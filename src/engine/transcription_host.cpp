#include "transcription_host.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace audiorgui {

namespace {

constexpr std::size_t kQueueSamples = 5 * asr::kSampleRate;     /* the audio a stream may wait in the queue */
constexpr std::size_t kQueueEvents  = 64;                       /* the events a stream may wait for the UI  */
constexpr int         kPollMs       = 20;                       /* the writer's wake-up interval            */
constexpr int         kCloseGraceMs = 1000;                     /* Stop() waits this much longer than the flush */

const wchar_t kDefaultProblem[] = L"The transcription engine reported a problem.";

}  // namespace

/* ---- the queues ------------------------------------------------------------ */

TranscriptionHost::SampleQueue::SampleQueue() : data_(kQueueSamples) {}

void TranscriptionHost::SampleQueue::Push(const std::int16_t *samples, std::size_t count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t capacity = data_.size();
    if (count >= capacity) {                            /* only the newest `capacity` samples survive */
        oldest_ += used_ + (count - capacity);
        samples += count - capacity;
        count = capacity;
        head_ = used_ = 0;
    } else if (count > capacity - used_) {              /* drop the oldest to make room */
        const std::size_t drop = count - (capacity - used_);
        head_ = (head_ + drop) % capacity;
        used_ -= drop;
        oldest_ += drop;
    }
    const std::size_t tail = (head_ + used_) % capacity;
    const std::size_t first = std::min(count, capacity - tail);
    std::memcpy(data_.data() + tail, samples, first * sizeof(std::int16_t));
    std::memcpy(data_.data(), samples + first, (count - first) * sizeof(std::int16_t));
    used_ += count;
}

std::size_t TranscriptionHost::SampleQueue::Read(std::int16_t *out, std::size_t maximum, std::uint64_t *firstSample)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t capacity = data_.size();
    const std::size_t n = std::min(maximum, used_);
    const std::size_t first = std::min(n, capacity - head_);
    *firstSample = oldest_;
    std::memcpy(out, data_.data() + head_, first * sizeof(std::int16_t));
    std::memcpy(out + first, data_.data(), (n - first) * sizeof(std::int16_t));
    head_ = (head_ + n) % capacity;
    used_ -= n;
    oldest_ += n;
    return n;
}

TranscriptionHost::EventQueue::EventQueue() : events_(kQueueEvents) {}

void TranscriptionHost::EventQueue::Push(const TranscriptEvent &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (used_ == events_.size()) {                      /* the UI is not looking: lose the oldest */
        head_ = (head_ + 1) % events_.size();
        --used_;
    }
    events_[(head_ + used_) % events_.size()] = event;
    ++used_;
}

std::size_t TranscriptionHost::EventQueue::Take(TranscriptEvent *out, std::size_t capacity)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t n = std::min(capacity, used_);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = events_[(head_ + i) % events_.size()];
    head_ = (head_ + n) % events_.size();
    used_ -= n;
    return n;
}

/* ---- the host -------------------------------------------------------------- */

TranscriptionHost::TranscriptionHost(TransportFactory factory, Options options)
    : factory_(std::move(factory)), options_(std::move(options))
{
    for (StepDownPolicy &policy : stepDown_)
        policy = StepDownPolicy(options_.stepDownHoldSeconds);
}

TranscriptionHost::~TranscriptionHost()
{
    Stop();
}

void TranscriptionHost::Start(std::uint8_t streams)
{
    const std::uint8_t wanted = static_cast<std::uint8_t>(streams & ((1u << kStreamCount) - 1));
    if (started_ || wanted == 0)
        return;
    started_ = true;
    streams_ = wanted;
    for (int s = 0; s < kStreamCount; ++s) {            /* the queues of the streams that are transcribed, and no others */
        if (Wanted(static_cast<StreamId>(s))) {
            lanes_[s].audio = std::make_unique<SampleQueue>();
            lanes_[s].events = std::make_unique<EventQueue>();
        }
    }
    SetAllStarting();
    thread_ = std::thread(&TranscriptionHost::Run, this);
}

void TranscriptionHost::Stop()
{
    if (!thread_.joinable())
        return;
    {
        std::unique_lock<std::mutex> lock(controlMutex_);
        stop_ = true;
        control_.notify_all();
        /* The supervisor normally ends by itself within the flush time.  If it is stuck (a helper that
         * no longer reads its pipe), closing the conversation frees it. */
        const bool ended = control_.wait_for(lock, std::chrono::milliseconds(options_.flushMs + kCloseGraceMs),
                                             [&] { return runDone_; });
        if (!ended && current_)
            current_->Close();
    }
    thread_.join();
}

void TranscriptionHost::Push(StreamId stream, const std::int16_t *samples, std::size_t count)
{
    if (Wanted(stream) && count > 0)
        lanes_[StreamIndex(stream)].audio->Push(samples, count);
}

TranscriptStatus TranscriptionHost::Status(StreamId stream) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return lanes_[StreamIndex(stream)].status;
}

std::wstring TranscriptionHost::Problem(StreamId stream) const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return lanes_[StreamIndex(stream)].problem;
}

std::size_t TranscriptionHost::TakeEvents(StreamId stream, TranscriptEvent *out, std::size_t capacity)
{
    const Lane &lane = lanes_[StreamIndex(stream)];
    return lane.events ? lane.events->Take(out, capacity) : 0;
}

/* ---- state ----------------------------------------------------------------- */

/* A stream whose transcription failed stays failed until the host is made anew. */
void TranscriptionHost::SetStatus(StreamId stream, const TranscriptStatus &status)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    Lane &lane = lanes_[StreamIndex(stream)];
    if (lane.status.state != TranscriptState::Failed) {
        lane.status = status;
        lane.status.smallerModel = smaller_;
    }
}

void TranscriptionHost::Fail(StreamId stream, const std::wstring &problem)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    Lane &lane = lanes_[StreamIndex(stream)];
    if (lane.status.state == TranscriptState::Failed)
        return;
    lane.status = TranscriptStatus{TranscriptState::Failed, 0.0f};
    lane.problem = problem.empty() ? kDefaultProblem : problem;
}

void TranscriptionHost::FailAll(const std::wstring &problem)
{
    for (int s = 0; s < kStreamCount; ++s) {
        if (Wanted(static_cast<StreamId>(s)))
            Fail(static_cast<StreamId>(s), problem);
    }
}

void TranscriptionHost::SetAllStarting()
{
    for (int s = 0; s < kStreamCount; ++s) {
        if (Wanted(static_cast<StreamId>(s)))
            SetStatus(static_cast<StreamId>(s), TranscriptStatus{TranscriptState::Starting, 0.0f});
    }
}

void TranscriptionHost::SetAllOff()
{
    for (int s = 0; s < kStreamCount; ++s)
        SetStatus(static_cast<StreamId>(s), TranscriptStatus());
}

bool TranscriptionHost::StopRequested()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return stop_;
}

bool TranscriptionHost::StepRequested()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    return stepRequested_;
}

bool TranscriptionHost::TakeStepRequest()
{
    std::lock_guard<std::mutex> lock(controlMutex_);
    const bool requested = stepRequested_;
    stepRequested_ = false;
    return requested;
}

/* True if the time passed, false if Stop() came first. */
bool TranscriptionHost::SleepUnlessStopped(int milliseconds)
{
    std::unique_lock<std::mutex> lock(controlMutex_);
    return !control_.wait_for(lock, std::chrono::milliseconds(milliseconds), [&] { return stop_; });
}

/* ---- the supervising thread ------------------------------------------------- */

void TranscriptionHost::Run()
{
    int restarts = 0;
    for (;;) {
        std::wstring problem;
        std::unique_ptr<IAsrTransport> transport = CurrentFactory()(&problem, [this] { return StopRequested(); });
        if (!transport) {
            if (!StopRequested())                       /* (a start given up on because of Stop() is not a failure) */
                FailAll(problem);
            break;
        }
        const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            current_ = transport.get();
            readerDone_ = ready_ = false;
        }
        std::thread reader(&TranscriptionHost::ReadLoop, this, transport.get());

        const bool finished = WriteLoop(*transport);    /* true: Stop() came and the helper has everything */
        if (finished) {                                 /* give it time to say its last words and hang up */
            std::unique_lock<std::mutex> lock(controlMutex_);
            control_.wait_for(lock, std::chrono::milliseconds(options_.flushMs), [&] { return readerDone_; });
        }
        transport->Close();
        reader.join();
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            current_ = nullptr;                         /* before the transport goes: see Stop() */
        }
        transport.reset();

        if (finished || StopRequested())
            break;

        /* Not gone by itself but let go, because it cannot keep up: the next model takes over at once. */
        if (TakeStepRequest()) {
            ++stage_;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                smaller_ = true;
            }
            for (Lane &lane : lanes_)
                lane.base = lane.next;
            for (StepDownPolicy &policy : stepDown_)
                policy.Reset();
            SetAllStarting();
            continue;
        }

        /* The helper went away on its own.  One that had been running well is not a helper that keeps
         * stopping: only crashes in quick succession use up the restarts. */
        if (std::chrono::steady_clock::now() - began > std::chrono::milliseconds(options_.healthyMs))
            restarts = 0;
        if (restarts >= options_.maxRestarts) {
            FailAll(L"The transcription engine kept stopping.");
            break;
        }
        for (Lane &lane : lanes_)
            lane.base = lane.next;                      /* a new helper counts its utterances from 0 again */
        SetAllStarting();
        if (!SleepUnlessStopped(options_.backoffMs << restarts))
            break;
        ++restarts;
    }
    SetAllOff();
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        runDone_ = true;
    }
    control_.notify_all();
}

/* Sends Hello, then the audio as it comes, until Stop() (true: the Stop message went out) or until
 * the helper cannot be written to or is not Ready in time (false). */
bool TranscriptionHost::WriteLoop(IAsrTransport &transport)
{
    std::uint8_t frame[asr::kMaxFrame];
    asr::Message message;
    auto send = [&] {
        const std::size_t size = asr::Encode(message, frame, sizeof(frame));
        return size > 0 && transport.Write(frame, size);
    };

    message.type = asr::Type::Hello;
    message.streams = streams_;
    message.language = options_.language;
    message.text = CurrentModel();
    message.vadModel = options_.vadModel;
    if (!send())
        return false;

    message.type = asr::Type::Audio;
    const auto began = std::chrono::steady_clock::now();
    for (;;) {
        const bool stopping = StopRequested();
        bool sent = false;
        for (int s = 0; s < kStreamCount; ++s) {        /* what is queued, 100 ms at a time */
            if (!Wanted(static_cast<StreamId>(s)))
                continue;
            message.stream = static_cast<StreamId>(s);
            while ((message.sampleCount = lanes_[s].audio->Read(message.audio, asr::kMaxAudioSamples,
                                                                &message.firstSample)) > 0) {
                if (!send())
                    return false;
                sent = true;
            }
        }
        if (stopping) {
            message.type = asr::Type::Stop;
            return send();
        }

        std::unique_lock<std::mutex> lock(controlMutex_);
        if (readerDone_)
            return false;                               /* the helper hung up (or was let go) before it was told to stop */
        if (!ready_ && std::chrono::steady_clock::now() - began > std::chrono::milliseconds(options_.readyTimeoutMs))
            return false;
        if (!sent)
            control_.wait_for(lock, std::chrono::milliseconds(kPollMs), [&] { return stop_ || readerDone_; });
    }
}

/* ---- the reader thread ------------------------------------------------------- */

void TranscriptionHost::ReadLoop(IAsrTransport *transport)
{
    asr::MessageReader reader;
    asr::Message message;
    std::uint8_t bytes[4096];
    bool ok = true;

    while (ok) {
        const std::size_t n = transport->Read(bytes, sizeof(bytes));
        if (n == 0)
            break;
        for (std::size_t used = 0; used < n && ok;) {
            const std::size_t took = reader.Append(bytes + used, n - used);
            used += took;
            for (;;) {
                const asr::MessageReader::Result result = reader.Next(&message);
                if (result == asr::MessageReader::Result::NeedMore) {
                    ok = took > 0;                      /* a full buffer without a whole message is corrupt */
                    break;
                }
                if (result == asr::MessageReader::Result::Invalid || !Handle(message) || StepRequested()) {
                    ok = false;                         /* (or too slow: see Handle) */
                    break;
                }
            }
        }
    }
    if (!ok)
        transport->Close();                             /* a helper that talks nonsense is let go: the writer restarts it */
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        readerDone_ = true;
    }
    control_.notify_all();
}

/* One message from the helper; false if it has no business sending it. */
bool TranscriptionHost::Handle(const asr::Message &m)
{
    switch (m.type) {
    case asr::Type::Ready: {
        {
            std::lock_guard<std::mutex> lock(controlMutex_);
            ready_ = true;
        }
        for (int s = 0; s < kStreamCount; ++s) {
            if (Wanted(static_cast<StreamId>(s)))
                SetStatus(static_cast<StreamId>(s), TranscriptStatus{TranscriptState::Live, 0.0f});
        }
        return true;
    }
    case asr::Type::Status:
        if (Wanted(m.stream)) {
            if (m.status.state == TranscriptState::Failed) {
                Fail(m.stream, std::wstring());         /* with the usual words, if no Problem comes */
            } else {
                SetStatus(m.stream, m.status);
                const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                if (stepDown_[StreamIndex(m.stream)].Observe(m.status.lagSeconds, now) && stage_ < options_.fallbacks.size()) {
                    std::lock_guard<std::mutex> lock(controlMutex_);
                    stepRequested_ = true;              /* the reader lets this helper go: see ReadLoop */
                }
            }
        }
        return true;
    case asr::Type::Event:
        if (Wanted(m.stream)) {
            Lane &lane = lanes_[StreamIndex(m.stream)];
            TranscriptEvent event = m.event;
            event.utterance += lane.base;
            lane.next = std::max(lane.next, event.utterance + 1);
            lane.events->Push(event);
        }
        return true;
    case asr::Type::Problem:
        if (m.all)
            FailAll(m.text);
        else if (Wanted(m.stream))
            Fail(m.stream, m.text);
        return true;
    default:
        return false;                                   /* Hello, Audio and Stop only go the other way */
    }
}

}  // namespace audiorgui
