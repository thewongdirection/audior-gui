#include "recording_session.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>

#include "hresult.h"
#include "mixer.h"
#include "peak_tap.h"
#include "pcm.h"
#include "ring_buffer.h"
#include "thread_support.h"

namespace audiorgui {

namespace {

constexpr DWORD       kPollMs     = 10;                             /* capture polling interval   */
constexpr DWORD       kWriteMs    = 100;                            /* writer wake-up interval    */
constexpr std::size_t kChunkFrames = kPcmRate / 4;                  /* most audio per sink write  */
constexpr std::size_t kRingBytes  = 8 * kPcmBytesPerSecond;         /* what a stream may buffer   */

/* Moves `frames` frames from a ring into `buffer`, padding with silence if the
 * ring holds fewer. */
void ReadFrames(RingBuffer &ring, std::vector<std::int16_t> &buffer, std::size_t frames)
{
    const std::size_t want = frames * kPcmFrameBytes;
    const std::size_t got = ring.Read(buffer.data(), want);
    if (got < want)
        std::memset(reinterpret_cast<std::uint8_t *>(buffer.data()) + got, 0, want - got);
}

}  // namespace

struct RecordingSession::Stream {
    Stream(StreamId stream, std::unique_ptr<ICaptureSource> s) : id(stream), source(std::move(s)), ring(kRingBytes) {}

    StreamId                        id;
    std::unique_ptr<ICaptureSource> source;
    RingBuffer                      ring;
    PeakAccumulator                 accumulator;
    PeakQueue                       peaks;
    SpeechDecimator                 decimator;          /* capture thread only */
    std::vector<std::int16_t>       speech;             /* ... and its scratch, reused */
    std::thread                     thread;
    HRESULT                         startHr = S_OK;     /* what opening the device gave */
};

/* One file, and the stream(s) it holds. */
struct RecordingSession::Output {
    std::unique_ptr<IAudioSink> sink;
    int                         first  = 0;
    int                         second = -1;            /* mixed in when merging */
};

SessionPlan MakePlan(const RecordingSettings &s, const std::wstring &outputPath)
{
    SessionPlan plan;
    plan.microphone = s.includeMicrophone;
    plan.system     = s.includeSystemAudio;
    plan.merge      = RecordsBoth(s) && s.mixMode == MixMode::Merge;
    plan.micWeight  = static_cast<float>(s.mergeMicPercent) / 100.0f;
    plan.files      = PlannedOutputFiles(s, outputPath);
    for (int i = 0; i < kStreamCount; ++i) {
        if (Transcribes(s, static_cast<StreamId>(i)))
            plan.speech = static_cast<std::uint8_t>(plan.speech | (1u << i));
    }
    return plan;
}

RecordingSession::RecordingSession(SourceFactory sources, SinkFactory sinks, HANDLE externalStop,
                                   ISpeechSink *speech)
    : makeSource_(std::move(sources)), makeSink_(std::move(sinks)), externalStop_(externalStop), speech_(speech)
{
}

RecordingSession::~RecordingSession()
{
    if (stop_) {
        RequestStop();
        Join();
        CloseHandle(stop_);
    }
}

bool RecordingSession::Start(const SessionPlan &plan, std::wstring *error)
{
    if (started_ || stop_ || (!plan.microphone && !plan.system)) {
        *error = L"The recording could not be started.";
        return false;
    }
    const bool both = plan.microphone && plan.system;
    if (plan.files.size() != (both && !plan.merge ? 2u : 1u)) {
        *error = L"The recording could not be started: the output files do not match the streams.";
        return false;
    }
    plan_ = plan;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_) {
        *error = WithHr(L"The recording could not be started", HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }

    try {
        int count = 0;
        for (int i = 0; i < kStreamCount; ++i) {
            if (!(i == StreamIndex(StreamId::Microphone) ? plan.microphone : plan.system))
                continue;
            std::unique_ptr<ICaptureSource> source = makeSource_(static_cast<StreamId>(i));
            if (!source)
                throw std::runtime_error("no capture source");
            streams_[i] = std::make_unique<Stream>(static_cast<StreamId>(i), std::move(source));
            ++count;
        }
        liveCaptures_ = count;
        for (int i = 0; i < kStreamCount; ++i) {
            if (streams_[i])
                streams_[i]->thread = std::thread(&RecordingSession::CaptureLoop, this, i);
        }

        /* Wait for every device to open (or fail to). */
        {
            std::unique_lock<std::mutex> lock(readyMutex_);
            readyCv_.wait(lock, [&] { return ready_ == count; });
        }
        for (int i = 0; i < kStreamCount; ++i) {
            if (streams_[i] && FAILED(streams_[i]->startHr)) {
                *error = WithHr(i == StreamIndex(StreamId::Microphone) ? L"Could not open the microphone"
                                                                       : L"Could not capture the playback device",
                                streams_[i]->startHr);
                Abort();
                return false;
            }
        }

        scratchA_.resize(kChunkFrames * kPcmChannels);
        scratchB_.resize(kChunkFrames * kPcmChannels);
        if (!OpenOutputs(error)) {
            Abort();
            return false;
        }
        writer_ = std::thread(&RecordingSession::WriteLoop, this);
    } catch (const std::exception &) {
        *error = L"The recording could not be started: out of resources.";
        Abort();
        return false;
    }
    started_ = true;
    return true;
}

bool RecordingSession::OpenOutputs(std::wstring *error)
{
    const bool both = plan_.microphone && plan_.system;
    for (std::size_t i = 0; i < plan_.files.size(); ++i) {
        Output out;
        if (both && plan_.merge) {
            out.first = StreamIndex(StreamId::Microphone);
            out.second = StreamIndex(StreamId::System);
        } else if (both) {
            out.first = static_cast<int>(i);                /* separate: the microphone's file, then the system's */
        } else {
            out.first = plan_.microphone ? StreamIndex(StreamId::Microphone) : StreamIndex(StreamId::System);
        }

        HRESULT hr = S_OK;
        out.sink = makeSink_(plan_.files[i], &hr);
        if (!out.sink) {
            *error = WithHr((L"Could not create \"" + plan_.files[i] + L"\"").c_str(), FAILED(hr) ? hr : E_FAIL);
            return false;
        }
        outputs_.push_back(std::move(out));
    }
    return true;
}

void RecordingSession::RequestStop()
{
    stopping_ = true;
    SetEvent(stop_);
}

void RecordingSession::Join()
{
    for (auto &stream : streams_) {
        if (stream && stream->thread.joinable())
            stream->thread.join();
    }
    if (writer_.joinable())
        writer_.join();
}

/* A start that failed half way: stop what is running and remove the files. */
void RecordingSession::Abort()
{
    RequestStop();
    Join();
    for (Output &out : outputs_)
        out.sink->Close(false);
    outputs_.clear();
}

void RecordingSession::Stop()
{
    if (!started_)
        return;
    RequestStop();
    Join();
}

void RecordingSession::Fail(const wchar_t *what, long hr)
{
    {
        std::lock_guard<std::mutex> lock(failureMutex_);
        if (failure_.empty())
            failure_ = WithHr(what, hr);
    }
    RequestStop();
}

std::wstring RecordingSession::Failure() const
{
    std::lock_guard<std::mutex> lock(failureMutex_);
    return failure_;
}

RecordingStats RecordingSession::Stats() const
{
    RecordingStats stats;
    stats.seconds = static_cast<double>(framesWritten_) / kPcmRate;
    for (const Output &out : outputs_)
        stats.bytesWritten += out.sink->BytesWritten();
    return stats;
}

std::size_t RecordingSession::TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity)
{
    Stream *s = streams_[StreamIndex(stream)].get();
    return s ? s->peaks.Take(out, capacity) : 0;
}

/* ---- capture ------------------------------------------------------------- */

/* Opens and starts the device.  Running out of memory is a failure to start like any other. */
HRESULT RecordingSession::OpenSource(Stream &stream)
{
    try {
        const HRESULT hr = stream.source->Open();
        return FAILED(hr) ? hr : stream.source->Start();
    } catch (const std::exception &) {
        return E_OUTOFMEMORY;
    }
}

/* Takes audio from the device until the recording is stopped or fails. */
void RecordingSession::PollLoop(Stream &stream)
{
    std::vector<std::int16_t> frames;
    std::vector<PeakBin> bins;
    for (;;) {
        const bool last = stopping_;                    /* one more poll empties the device's buffer */
        frames.clear();
        const HRESULT hr = stream.source->Poll(&frames);
        Deliver(stream, frames, &bins);
        if (FAILED(hr)) {
            Fail(L"Audio capture failed", hr);
            return;
        }
        if (last)
            return;
        WaitForSingleObject(stop_, kPollMs);
    }
}

void RecordingSession::CaptureLoop(int index)
{
    Stream &stream = *streams_[index];
    {
        ComScope com;
        AudioThreadScope scheduling;

        const HRESULT hr = OpenSource(stream);
        {
            std::lock_guard<std::mutex> lock(readyMutex_);
            stream.startHr = hr;
            ++ready_;
            readyCv_.notify_all();
        }

        if (SUCCEEDED(hr)) {
            try {
                PollLoop(stream);
            } catch (const std::exception &) {          /* a buffer could not grow: end the recording, keep the file */
                Fail(L"Recording ran out of memory", E_OUTOFMEMORY);
            }
        }
        stream.source->Stop();                          /* releases the device on this thread */
    }
    --liveCaptures_;
}

void RecordingSession::Deliver(Stream &stream, const std::vector<std::int16_t> &frames, std::vector<PeakBin> *bins)
{
    if (frames.empty())
        return;
    bins->clear();
    stream.accumulator.Add(frames.data(), frames.size() / kPcmChannels, bins);
    stream.peaks.Push(bins->data(), bins->size());

    if (speech_ && (plan_.speech & (1u << StreamIndex(stream.id)))) {      /* the transcriber, before the writer's ring */
        stream.speech.clear();
        stream.decimator.Add(frames.data(), frames.size() / kPcmChannels, &stream.speech);
        speech_->Push(stream.id, stream.speech.data(), stream.speech.size());
    }

    const std::size_t bytes = frames.size() * sizeof(std::int16_t);
    if (kRingBytes - stream.ring.Used() < bytes) {      /* the writer is not keeping up */
        Fail(L"Recording fell behind: the disk or encoder is too slow", HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
        return;
    }
    stream.ring.Write(frames.data(), bytes);
}

/* ---- writing ------------------------------------------------------------- */

void RecordingSession::WriteLoop()
{
    ComScope com;
    HANDLE waits[2] = {stop_, externalStop_};
    const DWORD waitCount = externalStop_ ? 2 : 1;

    for (;;) {
        if (stopping_) {
            Sleep(kPollMs);                             /* the captures are finishing */
        } else if (WaitForMultipleObjects(waitCount, waits, FALSE, kWriteMs) == WAIT_OBJECT_0 + 1) {
            RequestStop();                              /* another process asked */
        }
        /* When the captures are done everything they captured is in the rings. */
        const bool final = stopping_ && liveCaptures_ == 0;
        try {
            Drain(final);
        } catch (const std::exception &) {
            sinkFailed_ = true;
            Fail(L"Recording ran out of memory", E_OUTOFMEMORY);
        }
        if (final)
            break;
    }
    Finalise();
}

/* Writes what the rings hold.  Merging needs both streams, so it only takes
 * what both have (all of it once the recording is over: the shorter one is
 * padded with silence). */
void RecordingSession::Drain(bool final)
{
    for (std::size_t i = 0; i < outputs_.size() && !sinkFailed_; ++i) {
        Output &out = outputs_[i];
        Stream &first = *streams_[out.first];
        Stream *second = out.second >= 0 ? streams_[out.second].get() : nullptr;

        std::size_t frames = first.ring.Used() / kPcmFrameBytes;
        if (second) {
            const std::size_t other = second->ring.Used() / kPcmFrameBytes;
            frames = final ? std::max(frames, other) : std::min(frames, other);
        }

        while (frames > 0 && !sinkFailed_) {
            const std::size_t n = std::min(frames, kChunkFrames);
            ReadFrames(first.ring, scratchA_, n);
            if (second) {
                ReadFrames(second->ring, scratchB_, n);
                MixWeighted(scratchA_.data(), scratchA_.data(), scratchB_.data(), n * kPcmChannels, plan_.micWeight);
            }
            const HRESULT hr = out.sink->Write(scratchA_.data(), n * kPcmFrameBytes);
            if (FAILED(hr)) {
                sinkFailed_ = true;
                Fail(L"Failed to write audio data", hr);
                break;
            }
            if (i == 0)
                framesWritten_ += n;
            frames -= n;
        }
    }
}

void RecordingSession::Finalise()
{
    for (Output &out : outputs_) {
        const HRESULT hr = out.sink->Close(true);
        if (FAILED(hr))
            Fail(L"Failed to finalise output file", hr);
    }
}

}  // namespace audiorgui
