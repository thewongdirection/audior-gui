/*
 * helper.cpp - audior_asr.exe: the live transcription helper.
 *
 *     audior_asr.exe                          the recorder's helper: speaks the protocol of
 *                                             core/asr_protocol.h on standard input and output
 *     audior_asr.exe --file x.wav --model m   transcribes a 16 kHz mono 16-bit WAV through the same
 *       [--vad v] [--english] [--realtime]    pipeline and prints what the recorder would be told
 *                                             (--vad: the Silero model, to cut phrases with it;
 *                                             --english: take the speech for English;
 *                                             --realtime gives the audio at the pace of speech)
 *
 * The audio of each stream goes through an UtteranceTracker (where do phrases
 * begin and end), the phrases become jobs for one decoder thread (a DecodeQueue
 * decides what is worth doing when it falls behind), and whisper.cpp turns them
 * into text.  The recorder is told the text as it grows (Partial events, about
 * once a second per phrase) and once more when the speaker pauses (Final).
 *
 * The helper keeps nothing that grows with the length of the recording: a
 * phrase is at most 15 s of audio and only a few jobs wait.
 */
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/asr_protocol.h"
#include "../core/asr_text.h"
#include "../core/decode_queue.h"
#include "../core/lag_governor.h"
#include "../core/utterance_tracker.h"
#include "silero_classifier.h"
#include "whisper_decoder.h"

using namespace audiorgui;
using audiorasr::WhisperDecoder;

namespace {

double Now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string Utf8(const std::wstring &text)
{
    if (text.empty())
        return std::string();
    std::string out(static_cast<std::size_t>(WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                                                 nullptr, 0, nullptr, nullptr)),
                    '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], static_cast<int>(out.size()),
                        nullptr, nullptr);
    return out;
}

std::wstring FileNameOf(const std::wstring &path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

/* ---- where the results go ---- */

class IEvents {
public:
    virtual ~IEvents() = default;
    virtual void Ready(const std::wstring &what) = 0;
    virtual void Status(StreamId stream, TranscriptState state, float lagSeconds) = 0;
    virtual void Text(StreamId stream, std::uint32_t utterance, bool isFinal, double startSeconds,
                      const std::wstring &text) = 0;
    virtual void Problem(bool all, StreamId stream, const std::wstring &why) = 0;
};

/* To the recorder, as protocol frames on standard output.  If it cannot be written to, the recorder is gone. */
class ProtocolEvents final : public IEvents {
public:
    explicit ProtocolEvents(HANDLE out) : out_(out) {}

    void Ready(const std::wstring &what) override
    {
        asr::Message m;
        m.type = asr::Type::Ready;
        m.text = what;
        Send(m);
    }
    void Status(StreamId stream, TranscriptState state, float lagSeconds) override
    {
        asr::Message m;
        m.type = asr::Type::Status;
        m.stream = stream;
        m.status = TranscriptStatus{state, lagSeconds};
        Send(m);
    }
    void Text(StreamId stream, std::uint32_t utterance, bool isFinal, double startSeconds,
              const std::wstring &text) override
    {
        asr::Message m;
        m.type = asr::Type::Event;
        m.stream = stream;
        m.event.utterance = utterance;
        m.event.isFinal = isFinal;
        m.event.startSeconds = startSeconds;
        m.event.SetText(text.c_str(), text.size());
        Send(m);
    }
    void Problem(bool all, StreamId stream, const std::wstring &why) override
    {
        asr::Message m;
        m.type = asr::Type::Problem;
        m.all = all;
        m.stream = stream;
        m.text = why;
        Send(m);
    }

private:
    void Send(const asr::Message &m)
    {
        std::uint8_t frame[asr::kMaxFrame];
        const std::size_t size = asr::Encode(m, frame, sizeof(frame));
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t sent = 0; sent < size;) {
            DWORD written = 0;
            if (!WriteFile(out_, frame + sent, static_cast<DWORD>(size - sent), &written, nullptr) || written == 0)
                ExitProcess(1);                         /* nobody is listening any more */
            sent += written;
        }
    }

    HANDLE     out_;
    std::mutex mutex_;
};

/* To a person, as lines of text (the --file mode). */
class ConsoleEvents final : public IEvents {
public:
    explicit ConsoleEvents(HANDLE out) : out_(out) {}

    void Ready(const std::wstring &what) override { Line(L"ready: " + what); }
    void Status(StreamId, TranscriptState, float lagSeconds) override
    {
        wchar_t line[48];
        std::swprintf(line, sizeof(line) / sizeof(line[0]), L"  (lag %.1f s)", lagSeconds);
        Line(line);
    }
    void Text(StreamId stream, std::uint32_t utterance, bool isFinal, double startSeconds,
              const std::wstring &text) override
    {
        wchar_t head[80];
        std::swprintf(head, sizeof(head) / sizeof(head[0]), L"%ls #%u @%.1fs (%ls): ", isFinal ? L"final  " : L"partial",
                      utterance, startSeconds, stream == StreamId::Microphone ? L"mic" : L"system");
        Line(head + text);
    }
    void Problem(bool, StreamId, const std::wstring &why) override { Line(L"problem: " + why); }

private:
    void Line(const std::wstring &text)
    {
        const std::string bytes = Utf8(text + L"\r\n");
        std::lock_guard<std::mutex> lock(mutex_);
        DWORD written = 0;
        WriteFile(out_, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    }

    HANDLE     out_;
    std::mutex mutex_;
};

/* ---- the work ---- */

class Transcriber {
public:
    /* `classifiers`: who decides what is speech, per stream (a null one: loudness decides). */
    Transcriber(IEvents &events, WhisperDecoder &decoder, std::uint8_t streams, const TrackerConfig &config,
                std::unique_ptr<FrameClassifier> (&classifiers)[kStreamCount])
        : events_(events), decoder_(decoder), streams_(streams), worker_(&Transcriber::Work, this)
    {
        for (int s = 0; s < kStreamCount; ++s) {        /* (the worker does not look at the trackers) */
            if (streams_ & (1u << s))
                trackers_[s] = std::make_unique<UtteranceTracker>(config, std::move(classifiers[s]));
        }
    }

    ~Transcriber()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finishing_ = true;
            aborted_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable())                         /* (Finish() may have joined it already) */
            worker_.join();
    }

    /* The next audio of a stream, from the thread that reads the protocol. */
    void Audio(StreamId stream, std::uint64_t firstSample, const std::int16_t *samples, std::size_t count)
    {
        if (streams_ & (1u << StreamIndex(stream)))
            trackers_[StreamIndex(stream)]->Add(samples, count, firstSample, Sink(stream));
    }

    /* No more audio: what is being spoken is settled, everything waiting is decoded, and the worker ends. */
    void Finish()
    {
        for (int s = 0; s < kStreamCount; ++s) {
            if (streams_ & (1u << s))
                trackers_[s]->Flush(Sink(static_cast<StreamId>(s)));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finishing_ = true;
        }
        wake_.notify_all();
        worker_.join();
    }

private:
    UtteranceTracker::Sink Sink(StreamId stream)
    {
        return [this, stream](const UtteranceTracker::Step &step) {
            DecodeJob job;
            job.stream = stream;
            job.id = step.id;
            job.startSample = step.startSample;
            job.isFinal = step.kind == UtteranceTracker::Step::Kind::Final;
            job.receivedAt = Now();
            job.audio.assign(step.audio, step.audio + step.count);
            DecodeQueue::Lost lost;
            bool dropped;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                dropped = queue_.Push(std::move(job), &lost);
            }
            wake_.notify_one();
            if (dropped)                                /* too far behind to decode it: the gap is shown, not hidden */
                events_.Text(lost.stream, lost.id, true, static_cast<double>(lost.startSample) / asr::kSampleRate, L"…");
        };
    }

    void Work()
    {
        bool failed[kStreamCount] = {false, false};     /* streams the recorder was told are over: nothing more is decoded for them */
        for (;;) {
            DecodeJob job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return aborted_ || finishing_ || !queue_.Empty(); });
                if (aborted_ || !queue_.Pop(&job))
                    return;                             /* (finishing, and nothing is left) */
            }

            std::string utf8;
            const int s = StreamIndex(job.stream);
            if (failed[s])
                continue;                               /* the recorder has been told this stream is over */
            if (!decoder_.Decode(job.audio.data(), job.audio.size(), &utf8)) {
                failed[s] = true;
                events_.Problem(false, job.stream, L"The speech engine failed on some audio.");
                continue;
            }
            bool looped = false;
            std::wstring text = RemoveRepeatedPhrases(CleanTranscript(WideFromUtf8(utf8)), &looped);
            if (looped && !job.isFinal)
                text.clear();                           /* a draft that went round in circles is not shown */
            if (job.isFinal || !text.empty())           /* an empty Final takes back a phrase that was nothing */
                events_.Text(job.stream, job.id, job.isFinal, static_cast<double>(job.startSample) / asr::kSampleRate,
                             text);
            const float lag = static_cast<float>(std::max(0.0, Now() - job.receivedAt));
            events_.Status(job.stream, governors_[s].Observe(lag), lag);
        }
    }

    IEvents          &events_;
    WhisperDecoder   &decoder_;
    const std::uint8_t streams_;
    std::unique_ptr<UtteranceTracker> trackers_[kStreamCount];      /* of the streams transcribed; the protocol thread only */
    LagGovernor      governors_[kStreamCount];          /* the worker only */

    std::mutex              mutex_;
    std::condition_variable wake_;
    DecodeQueue             queue_;
    bool                    finishing_ = false, aborted_ = false;
    std::thread             worker_;                    /* last: it starts using everything above */
};

/* Loads the model the recorder asked for and starts the work.  False after telling the recorder why not. */
bool Begin(const asr::Message &hello, IEvents &events, WhisperDecoder &decoder, std::unique_ptr<Transcriber> *work)
{
    std::string error;
    if (!decoder.Load(Utf8(hello.text), hello.language == SpeechLanguage::English, &error)) {
        events.Problem(true, StreamId::Microphone, WideFromUtf8(error));
        return false;
    }

    /* Phrases are cut where Silero hears speech if its model is there and loads; otherwise by loudness. */
    std::unique_ptr<FrameClassifier> classifiers[kStreamCount];
    bool silero = !hello.vadModel.empty();
    for (int s = 0; s < kStreamCount && silero; ++s) {
        if (hello.streams & (1u << s)) {
            classifiers[s] = audiorasr::MakeSileroClassifier(Utf8(hello.vadModel));
            silero = classifiers[s] != nullptr;
        }
    }
    if (!silero) {
        for (std::unique_ptr<FrameClassifier> &classifier : classifiers)
            classifier.reset();
    }

    events.Ready(FileNameOf(hello.text) + L" · CPU, " + std::to_wstring(decoder.Threads()) + L" threads" +
                 (silero ? L" · Silero" : L""));
    *work = std::make_unique<Transcriber>(events, decoder, hello.streams,
                                          silero ? audiorasr::SileroTrackerConfig() : TrackerConfig(), classifiers);
    return true;
}

/* ---- the recorder's helper ---- */

int RunProtocol()
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
    ProtocolEvents events(out);
    WhisperDecoder decoder;
    std::unique_ptr<Transcriber> work;
    asr::MessageReader reader;
    asr::Message message;
    std::uint8_t bytes[4096];

    for (;;) {
        DWORD n = 0;
        if (!ReadFile(in, bytes, sizeof(bytes), &n, nullptr) || n == 0)
            ExitProcess(0);                             /* the recorder is gone: nothing is owed to anyone */
        for (DWORD used = 0; used < n;) {
            used += static_cast<DWORD>(reader.Append(bytes + used, n - used));
            for (;;) {
                const asr::MessageReader::Result result = reader.Next(&message);
                if (result == asr::MessageReader::Result::NeedMore)
                    break;
                if (result == asr::MessageReader::Result::Invalid)
                    return 2;

                switch (message.type) {
                case asr::Type::Hello:
                    if (work || message.version != asr::kVersion) {
                        events.Problem(true, StreamId::Microphone, L"The recorder and the helper do not speak the same version.");
                        return 2;
                    }
                    if (!Begin(message, events, decoder, &work))
                        return 3;
                    break;
                case asr::Type::Audio:
                    if (!work)
                        return 2;                       /* audio before Hello */
                    work->Audio(message.stream, message.firstSample, message.audio, message.sampleCount);
                    break;
                case asr::Type::Stop:
                    if (work)
                        work->Finish();
                    return 0;
                default:
                    return 2;                           /* things only a helper says */
                }
            }
        }
    }
}

/* ---- --file ---- */

bool ReadWav(const std::wstring &path, std::vector<std::int16_t> *samples, std::wstring *error)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    LARGE_INTEGER size = {};
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size) || size.QuadPart < 44 || size.QuadPart > (256 << 20)) {
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        *error = L"The WAV file cannot be read.";
        return false;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size.QuadPart));
    DWORD got = 0;
    const bool read = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &got, nullptr) && got == data.size();
    CloseHandle(file);
    if (!read || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        *error = L"That is not a WAV file.";
        return false;
    }

    bool format = false;
    for (std::size_t at = 12; at + 8 <= data.size();) {
        std::uint32_t length;
        std::memcpy(&length, data.data() + at + 4, 4);
        const std::uint8_t *body = data.data() + at + 8;
        if (std::memcmp(data.data() + at, "fmt ", 4) == 0 && length >= 16 && at + 8 + length <= data.size()) {
            std::uint16_t tag, channels, bits;
            std::uint32_t rate;
            std::memcpy(&tag, body, 2);
            std::memcpy(&channels, body + 2, 2);
            std::memcpy(&rate, body + 4, 4);
            std::memcpy(&bits, body + 14, 2);
            format = tag == 1 && channels == 1 && rate == 16000 && bits == 16;
        } else if (std::memcmp(data.data() + at, "data", 4) == 0) {
            if (!format) {
                *error = L"The WAV file must be 16 kHz, mono, 16-bit.";
                return false;
            }
            const std::size_t bytesAvailable = std::min<std::size_t>(length, data.size() - (at + 8));
            samples->resize(bytesAvailable / 2);
            std::memcpy(samples->data(), body, samples->size() * 2);
            return true;
        }
        at += 8 + static_cast<std::size_t>(length) + (length & 1);
    }
    *error = L"The WAV file has no audio.";
    return false;
}

/* `realtime`: the audio is given at the pace of speech (100 ms every 100 ms), as the recorder does, instead of at once. */
int RunFile(const std::wstring &wav, const std::wstring &model, const std::wstring &vad, SpeechLanguage language, bool realtime)
{
    ConsoleEvents events(GetStdHandle(STD_OUTPUT_HANDLE));
    std::vector<std::int16_t> samples;
    std::wstring error;
    if (!ReadWav(wav, &samples, &error)) {
        events.Problem(true, StreamId::Microphone, error);
        return 1;
    }
    asr::Message hello;
    hello.type = asr::Type::Hello;
    hello.streams = 1;
    hello.language = language;
    hello.vadModel = vad;
    hello.text = model;
    WhisperDecoder decoder;
    std::unique_ptr<Transcriber> work;
    if (!Begin(hello, events, decoder, &work))
        return 3;

    constexpr std::size_t kChunk = 1600;                /* what the recorder sends: 100 ms */
    for (std::size_t at = 0; at < samples.size(); at += kChunk) {
        work->Audio(StreamId::Microphone, at, samples.data() + at, std::min(kChunk, samples.size() - at));
        if (realtime)
            Sleep(static_cast<DWORD>(kChunk * 1000 / asr::kSampleRate));
    }
    work->Finish();
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t **argv)
{
    /* Whatever DLLs are loaded from now on come from Windows' own folder, not from the current directory. */
    SetDllDirectoryW(L"");
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

    if (argc == 1)
        return RunProtocol();
    std::wstring wav, model, vad;
    bool realtime = false, understood = true;
    SpeechLanguage language = SpeechLanguage::Auto;
    for (int i = 1; i < argc && understood; ++i) {
        const std::wstring option = argv[i];
        if (option == L"--realtime")
            realtime = true;
        else if (option == L"--english")
            language = SpeechLanguage::English;
        else if (i + 1 < argc && option == L"--file")
            wav = argv[++i];
        else if (i + 1 < argc && option == L"--model")
            model = argv[++i];
        else if (i + 1 < argc && option == L"--vad")
            vad = argv[++i];
        else
            understood = false;
    }
    if (!understood || wav.empty() || model.empty()) {
        const char usage[] = "usage: audior_asr.exe --file <16 kHz mono 16-bit WAV> --model <ggml model> [--vad <silero model>] [--english] [--realtime]\r\n"
                             "(without arguments it is the recorder's helper and speaks on standard input and output)\r\n";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), usage, sizeof(usage) - 1, &written, nullptr);
        return 1;
    }
    return RunFile(wav, model, vad, language, realtime);
}
