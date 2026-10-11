/* Tests of the transcription host, against a fake helper on in-memory pipes. */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../src/engine/transcript_file.h"
#include "../src/engine/transcription_host.h"
#include "temp_dir.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

using Clock = std::chrono::steady_clock;

template <typename Pred>
bool WaitUntil(Pred done, int timeoutMs = 5000)
{
    const Clock::time_point end = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < end) {
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

/* ---- a byte pipe between two threads ---- */

class Pipe {
public:
    explicit Pipe(std::size_t capacity = static_cast<std::size_t>(-1)) : capacity_(capacity) {}

    /* Waits while the pipe is full; false once it is closed. */
    bool Write(const void *data, std::size_t count)
    {
        const std::uint8_t *p = static_cast<const std::uint8_t *>(data);
        std::unique_lock<std::mutex> lock(mutex_);
        while (count > 0) {
            changed_.wait(lock, [&] { return closed_ || bytes_.size() < capacity_; });
            if (closed_)
                return false;
            const std::size_t take = std::min(count, capacity_ - bytes_.size());
            bytes_.insert(bytes_.end(), p, p + take);
            p += take;
            count -= take;
            changed_.notify_all();
        }
        return true;
    }

    /* Waits for bytes; 0 once the pipe is closed and empty. */
    std::size_t Read(void *out, std::size_t capacity)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        changed_.wait(lock, [&] { return closed_ || !bytes_.empty(); });
        const std::size_t n = std::min(capacity, bytes_.size());
        std::copy(bytes_.begin(), bytes_.begin() + static_cast<std::ptrdiff_t>(n), static_cast<std::uint8_t *>(out));
        bytes_.erase(bytes_.begin(), bytes_.begin() + static_cast<std::ptrdiff_t>(n));
        changed_.notify_all();
        return n;
    }

    void Close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        changed_.notify_all();
    }

    bool Closed()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    std::mutex              mutex_;
    std::condition_variable changed_;
    std::deque<std::uint8_t> bytes_;
    std::size_t             capacity_;
    bool                    closed_ = false;
};

/* What the fake helper saw. */
struct Seen {
    asr::Type     type;
    StreamId      stream;
    std::uint64_t firstSample;
    std::size_t   sampleCount;
    std::uint8_t  streams;
    std::wstring  text;
    SpeechLanguage language;
    std::wstring  vadModel;
    bool          gpu;
};

/* One run of the fake helper: its pipes, its thread and what it saw. */
struct Instance {
    explicit Instance(std::size_t pipeCapacity) : in(std::make_shared<Pipe>(pipeCapacity)), out(std::make_shared<Pipe>()) {}

    std::shared_ptr<Pipe> in, out;                      /* recorder -> helper, helper -> recorder */
    std::thread           thread;
    mutable std::mutex    mutex;
    std::vector<Seen>     seen;

    void Record(const asr::Message &m)
    {
        std::lock_guard<std::mutex> lock(mutex);
        seen.push_back({m.type, m.stream, m.firstSample, m.sampleCount, m.streams, m.text, m.language, m.vadModel, m.gpu});
    }
    std::vector<Seen> Seen_() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return seen;
    }
    std::size_t Count(asr::Type type) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return static_cast<std::size_t>(std::count_if(seen.begin(), seen.end(), [&](const Seen &s) { return s.type == type; }));
    }
};

bool Send(Instance &helper, const asr::Message &m)
{
    std::vector<std::uint8_t> frame(asr::kMaxFrame);
    const std::size_t n = asr::Encode(m, frame.data(), frame.size());
    return n > 0 && helper.out->Write(frame.data(), n);
}

asr::Message Ready()
{
    asr::Message m;
    m.type = asr::Type::Ready;
    m.text = L"fake helper";
    return m;
}

asr::Message EventFor(StreamId stream, std::uint32_t id, bool isFinal, const wchar_t *text)
{
    asr::Message m;
    m.type = asr::Type::Event;
    m.stream = stream;
    m.event.utterance = id;
    m.event.isFinal = isFinal;
    m.event.startSeconds = id;
    m.event.SetText(text, std::wcslen(text));
    return m;
}

asr::Message StatusFor(StreamId stream, TranscriptState state, float lag)
{
    asr::Message m;
    m.type = asr::Type::Status;
    m.stream = stream;
    m.status.state = state;
    m.status.lagSeconds = lag;
    return m;
}

asr::Message ProblemFor(bool all, StreamId stream, const wchar_t *text)
{
    asr::Message m;
    m.type = asr::Type::Problem;
    m.all = all;
    m.stream = stream;
    m.text = text;
    return m;
}

/* Reads the recorder's messages until `react` says to hang up or the pipe closes. */
void Serve(Instance &helper, const std::function<bool(const asr::Message &)> &react)
{
    asr::MessageReader reader;
    asr::Message m;
    std::uint8_t bytes[4096];
    for (;;) {
        const std::size_t n = helper.in->Read(bytes, sizeof(bytes));
        if (n == 0)
            return;
        for (std::size_t used = 0; used < n;) {
            used += reader.Append(bytes + used, n - used);
            while (reader.Next(&m) == asr::MessageReader::Result::Message) {
                helper.Record(m);
                if (!react(m))
                    return;
            }
        }
    }
}

class FakeTransport final : public IAsrTransport {
public:
    explicit FakeTransport(Instance &helper) : helper_(helper) {}
    bool Write(const void *data, std::size_t count) override { return helper_.in->Write(data, count); }
    std::size_t Read(void *buffer, std::size_t capacity) override { return helper_.out->Read(buffer, capacity); }
    void Close() override
    {
        helper_.in->Close();
        helper_.out->Close();
    }

private:
    Instance &helper_;
};

/* Makes the transports (and the helper threads behind them) for a host. */
class FakeHelpers {
public:
    /* What helper number `index` does, on its own thread.  Returning is "the process exits". */
    std::function<void(Instance &, int)> behavior;
    std::size_t                          pipeCapacity = static_cast<std::size_t>(-1);
    bool                                 cannotStart = false;

    ~FakeHelpers()
    {
        for (auto &helper : instances_) {
            helper->in->Close();
            helper->out->Close();
        }
        for (auto &helper : instances_) {
            if (helper->thread.joinable())
                helper->thread.join();
        }
    }

    TransportFactory Factory()
    {
        return [this](std::wstring *problem, const StopCheck &) -> std::unique_ptr<IAsrTransport> {
            if (cannotStart) {
                *problem = L"audior_asr.exe was not found.";
                return nullptr;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            const int index = static_cast<int>(instances_.size());
            instances_.push_back(std::make_unique<Instance>(pipeCapacity));
            Instance &helper = *instances_.back();
            helper.thread = std::thread([this, &helper, index] {
                behavior(helper, index);
                helper.in->Close();                     /* the process is gone: its pipes close */
                helper.out->Close();
            });
            return std::make_unique<FakeTransport>(helper);
        };
    }

    std::size_t Started() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return instances_.size();
    }
    Instance &At(std::size_t i)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return *instances_[i];
    }

private:
    mutable std::mutex                    mutex_;
    std::vector<std::unique_ptr<Instance>> instances_;
};

/* The usual helper: Ready on Hello, goodbye on Stop. */
void WellBehaved(Instance &helper, int)
{
    Serve(helper, [&](const asr::Message &m) {
        if (m.type == asr::Type::Hello)
            Send(helper, Ready());
        return m.type != asr::Type::Stop;
    });
}

TranscriptionHost::Options Quick()
{
    TranscriptionHost::Options o;
    o.model = L"C:\\Models\\fake.bin";
    o.backoffMs = 5;
    o.flushMs = 500;
    return o;
}

constexpr std::uint8_t kMic = 1, kSystem = 2;

std::vector<std::int16_t> Ramp(std::size_t count, std::int16_t from = 0)
{
    std::vector<std::int16_t> v(count);
    for (std::size_t i = 0; i < count; ++i)
        v[i] = static_cast<std::int16_t>(from + static_cast<int>(i % 20000));
    return v;
}

std::vector<TranscriptEvent> TakeAll(TranscriptionHost &host, StreamId stream)
{
    std::vector<TranscriptEvent> all;
    TranscriptEvent chunk[8];
    for (std::size_t n; (n = host.TakeEvents(stream, chunk, 8)) > 0;)
        all.insert(all.end(), chunk, chunk + n);
    return all;
}

}  // namespace

TEST(Host_TellsTheHelperTheLanguage)
{
    for (SpeechLanguage language : {SpeechLanguage::Auto, SpeechLanguage::English}) {
        FakeHelpers helpers;
        helpers.behavior = WellBehaved;
        TranscriptionHost::Options options = Quick();
        options.language = language;
        options.vadModel = language == SpeechLanguage::English ? L"C:\\Models\\vad.bin" : L"";
        options.gpu = language == SpeechLanguage::Auto;
        TranscriptionHost host(helpers.Factory(), options);
        host.Start(kMic);
        host.Stop();
        const std::vector<Seen> seen = helpers.At(0).Seen_();
        CHECK(!seen.empty() && seen.front().type == asr::Type::Hello);
        CHECK(seen.front().language == language);
        CHECK_EQ(seen.front().vadModel, options.vadModel);      /* the voice-activity model too, or none */
        CHECK_EQ(seen.front().gpu, options.gpu);                /* and whether the graphics card may be used */
    }
}

TEST(Host_SendsHelloThenTheAudioThenStop)
{
    FakeHelpers helpers;
    helpers.behavior = WellBehaved;
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic);

    const std::vector<std::int16_t> audio = Ramp(16000);
    for (std::size_t at = 0; at < audio.size(); at += 160)
        host.Push(StreamId::Microphone, audio.data() + at, 160);
    host.Stop();

    CHECK_EQ(helpers.Started(), std::size_t(1));
    const std::vector<Seen> seen = helpers.At(0).Seen_();
    CHECK(seen.size() >= 3);
    CHECK(seen.front().type == asr::Type::Hello);
    CHECK_EQ(int(seen.front().streams), int(kMic));
    CHECK_EQ(seen.front().text, std::wstring(L"C:\\Models\\fake.bin"));
    CHECK(seen.back().type == asr::Type::Stop);

    std::uint64_t expected = 0;
    for (const Seen &s : seen) {
        if (s.type != asr::Type::Audio)
            continue;
        CHECK(s.stream == StreamId::Microphone);
        CHECK(s.sampleCount > 0 && s.sampleCount <= asr::kMaxAudioSamples);
        CHECK_EQ(s.firstSample, expected);              /* the audio arrives whole, in order */
        expected += s.sampleCount;
    }
    CHECK_EQ(expected, std::uint64_t(16000));
}

TEST(Host_OnlyTheRequestedStreamsAreSent)
{
    FakeHelpers helpers;
    helpers.behavior = WellBehaved;
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kSystem);

    const std::vector<std::int16_t> audio = Ramp(3200);
    host.Push(StreamId::Microphone, audio.data(), audio.size());       /* nobody asked for this one */
    host.Push(StreamId::System, audio.data(), audio.size());
    host.Stop();

    std::uint64_t system = 0;
    for (const Seen &s : helpers.At(0).Seen_()) {
        CHECK(!(s.type == asr::Type::Audio && s.stream == StreamId::Microphone));
        if (s.type == asr::Type::Audio)
            system += s.sampleCount;
    }
    CHECK_EQ(system, std::uint64_t(3200));
}

TEST(Host_StatusFollowsTheHelper)
{
    std::mutex gate;
    std::condition_variable opened;
    bool open = false;

    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello) {
                std::unique_lock<std::mutex> lock(gate);
                opened.wait(lock, [&] { return open; });                /* the model is "loading" */
                Send(helper, Ready());
                Send(helper, StatusFor(StreamId::Microphone, TranscriptState::Live, 0.7f));
            }
            return m.type != asr::Type::Stop;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    CHECK(host.Status(StreamId::Microphone).state == TranscriptState::Off);       /* before Start */
    host.Start(kMic);

    CHECK(host.Status(StreamId::Microphone).state == TranscriptState::Starting);
    CHECK(host.Status(StreamId::System).state == TranscriptState::Off);           /* not asked for */
    WaitUntil([&] { return helpers.Started() == 1; });
    {
        std::lock_guard<std::mutex> lock(gate);
        open = true;
    }
    opened.notify_all();

    CHECK(WaitUntil([&] {
        const TranscriptStatus s = host.Status(StreamId::Microphone);
        return s.state == TranscriptState::Live && s.lagSeconds > 0.69f && s.lagSeconds < 0.71f;
    }));
    host.Stop();
    CHECK(host.Status(StreamId::Microphone).state == TranscriptState::Off);       /* finished */
}

TEST(Host_EventsGoToTheirStreamInOrder)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello) {
                Send(helper, Ready());
                Send(helper, EventFor(StreamId::Microphone, 0, false, L"hel"));
                Send(helper, EventFor(StreamId::System, 0, true, L"over there"));
                Send(helper, EventFor(StreamId::Microphone, 0, true, L"hello"));
            }
            return m.type != asr::Type::Stop;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic | kSystem);

    CHECK(WaitUntil([&] { return host.Status(StreamId::System).state == TranscriptState::Live; }));
    std::vector<TranscriptEvent> mic, system;
    CHECK(WaitUntil([&] {
        for (const TranscriptEvent &e : TakeAll(host, StreamId::Microphone))
            mic.push_back(e);
        for (const TranscriptEvent &e : TakeAll(host, StreamId::System))
            system.push_back(e);
        return mic.size() >= 2 && !system.empty();
    }));
    host.Stop();

    CHECK_EQ(mic.size(), std::size_t(2));
    CHECK_EQ(std::wstring(mic[0].text), std::wstring(L"hel"));
    CHECK(!mic[0].isFinal);
    CHECK_EQ(std::wstring(mic[1].text), std::wstring(L"hello"));
    CHECK(mic[1].isFinal);
    CHECK_EQ(system.size(), std::size_t(1));
    CHECK_EQ(std::wstring(system[0].text), std::wstring(L"over there"));
}

TEST(Host_TellsTheArchiveOfEveryEventInOrder)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello) {
                Send(helper, Ready());
                Send(helper, EventFor(StreamId::Microphone, 0, false, L"hel"));
                Send(helper, EventFor(StreamId::System, 3, true, L"over there"));
                Send(helper, EventFor(StreamId::Microphone, 0, true, L"hello"));
                Send(helper, EventFor(StreamId::Microphone, 1, true, L"and goodbye"));
            }
            return m.type != asr::Type::Stop;
        });
    };
    struct Told {
        StreamId     stream;
        bool         isFinal;
        double       start;
        std::wstring text;
    };
    std::mutex mutex;
    std::vector<Told> told;
    TranscriptionHost::Options options = Quick();
    options.onEvent = [&](StreamId stream, const TranscriptEvent &e) {
        std::lock_guard<std::mutex> lock(mutex);
        told.push_back({stream, e.isFinal, e.startSeconds, e.text});
    };
    TranscriptionHost host(helpers.Factory(), options);
    host.Start(kMic | kSystem);

    CHECK(WaitUntil([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return told.size() >= 4;
    }));
    host.Stop();

    std::lock_guard<std::mutex> lock(mutex);
    CHECK_EQ(told.size(), std::size_t(4));                      /* drafts too, in the order they came */
    CHECK(told[0].stream == StreamId::Microphone && !told[0].isFinal);
    CHECK_EQ(told[0].text, std::wstring(L"hel"));
    CHECK(told[1].stream == StreamId::System && told[1].isFinal);
    CHECK_EQ(told[1].text, std::wstring(L"over there"));
    CHECK_NEAR(told[1].start, 3.0, 0.0);
    CHECK_EQ(told[2].text, std::wstring(L"hello"));
    CHECK_EQ(told[3].text, std::wstring(L"and goodbye"));
}

TEST(Host_TheArchiveGetsEveryEventEvenIfNobodyTakesThem)
{
    constexpr int kUtterances = 400;                            /* far more than the queue the window reads from keeps */
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello) {
                Send(helper, Ready());
                for (int i = 0; i < kUtterances; ++i)
                    Send(helper, EventFor(StreamId::Microphone, static_cast<std::uint32_t>(i), true, L"words"));
            }
            return m.type != asr::Type::Stop;
        });
    };
    std::atomic<int> count{0};
    TranscriptionHost::Options options = Quick();
    options.onEvent = [&](StreamId, const TranscriptEvent &) { ++count; };
    TranscriptionHost host(helpers.Factory(), options);
    host.Start(kMic);                                           /* and nothing ever calls TakeEvents */

    CHECK(WaitUntil([&] { return count.load() >= kUtterances; }));
    host.Stop();
    CHECK_EQ(count.load(), kUtterances);
}

TEST(Host_TheLastWordsAtStopReachTheTranscriptFileBeforeItIsClosed)
{
    /* The order WasapiEngine::Stop() uses: the host stops (the helper says its last words, which the host
     * passes on), and only then is the file closed.  The other way round, the end of the talk is lost. */
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello) {
                Send(helper, Ready());
                Send(helper, EventFor(StreamId::Microphone, 0, true, L"Settled early."));
                Send(helper, EventFor(StreamId::Microphone, 1, false, L"still being sp"));
            }
            if (m.type == asr::Type::Stop) {
                Send(helper, EventFor(StreamId::Microphone, 1, true, L"Said right at the end."));
                return false;
            }
            return true;
        });
    };
    TempDir dir;
    TranscriptFile file(dir.Path(L"t.txt"));
    TranscriptionHost::Options options = Quick();
    options.onEvent = [&](StreamId, const TranscriptEvent &e) { file.Add(e); };
    TranscriptionHost host(helpers.Factory(), options);
    host.Start(kMic);
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Live; }));

    host.Stop();
    file.Close();

    const std::vector<std::uint8_t> bytes = ReadFileBytes(dir.Path(L"t.txt"));
    CHECK_EQ(std::string(bytes.begin(), bytes.end()),
             std::string("\xEF\xBB\xBF[0:00] Settled early.\r\n[0:01] Said right at the end.\r\n"));
}

TEST(Host_AnEventForAStreamNobodyAskedForIsDropped)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello) {
                Send(helper, Ready());
                Send(helper, EventFor(StreamId::System, 0, true, L"uninvited"));
            }
            return m.type != asr::Type::Stop;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic);
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Live; }));
    host.Stop();
    CHECK(TakeAll(host, StreamId::System).empty());
}

TEST(Host_StopLetsTheHelperSayItsLastWords)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello)
                Send(helper, Ready());
            if (m.type == asr::Type::Stop) {
                Send(helper, EventFor(StreamId::Microphone, 7, true, L"the last words"));
                return false;                                           /* and it exits */
            }
            return true;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic);
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Live; }));
    host.Stop();

    const std::vector<TranscriptEvent> events = TakeAll(host, StreamId::Microphone);
    CHECK_EQ(events.size(), std::size_t(1));
    if (!events.empty())
        CHECK_EQ(std::wstring(events[0].text), std::wstring(L"the last words"));
}

TEST(Host_AProblemFailsOnlyItsStream)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello) {
                Send(helper, Ready());
                Send(helper, ProblemFor(false, StreamId::System, L"Out of memory on the GPU."));
            }
            return m.type != asr::Type::Stop;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic | kSystem);

    CHECK(WaitUntil([&] { return host.Status(StreamId::System).state == TranscriptState::Failed; }));
    CHECK_EQ(host.Problem(StreamId::System), std::wstring(L"Out of memory on the GPU."));
    CHECK(host.Status(StreamId::Microphone).state == TranscriptState::Live);
    host.Stop();
    CHECK(host.Status(StreamId::System).state == TranscriptState::Failed);        /* stays marked after Stop */
    CHECK(host.Status(StreamId::Microphone).state == TranscriptState::Off);
}

TEST(Host_AFailedStatusWithoutAProblemStillSaysSomething)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello)
                Send(helper, StatusFor(StreamId::Microphone, TranscriptState::Failed, 0.0f));
            return m.type != asr::Type::Stop;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic);
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Failed; }));
    CHECK(!host.Problem(StreamId::Microphone).empty());
    host.Stop();
}

TEST(Host_AProblemAboutEverythingFailsEveryStream)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello)
                Send(helper, ProblemFor(true, StreamId::Microphone, L"The model could not be loaded."));
            return m.type != asr::Type::Stop;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic | kSystem);
    CHECK(WaitUntil([&] {
        return host.Status(StreamId::Microphone).state == TranscriptState::Failed &&
               host.Status(StreamId::System).state == TranscriptState::Failed;
    }));
    CHECK_EQ(host.Problem(StreamId::System), std::wstring(L"The model could not be loaded."));
    host.Stop();
}

TEST(Host_ARestartedHelperContinuesTheNumbering)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int index) {
        int audio = 0;
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello)
                Send(helper, Ready());
            if (m.type == asr::Type::Audio && ++audio == 1)
                Send(helper, EventFor(StreamId::Microphone, 0, true, index == 0 ? L"before" : L"after"));
            if (m.type == asr::Type::Audio && index == 0 && audio == 3)
                return false;                                           /* the first helper dies */
            return m.type != asr::Type::Stop;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic);

    const std::vector<std::int16_t> audio = Ramp(160);
    std::vector<TranscriptEvent> events;
    CHECK(WaitUntil([&] {
        host.Push(StreamId::Microphone, audio.data(), audio.size());
        for (const TranscriptEvent &e : TakeAll(host, StreamId::Microphone))
            events.push_back(e);
        return events.size() >= 2;
    }));
    host.Stop();

    CHECK_EQ(helpers.Started(), std::size_t(2));
    CHECK_EQ(events.size(), std::size_t(2));
    if (events.size() == 2) {
        CHECK_EQ(std::wstring(events[0].text), std::wstring(L"before"));
        CHECK_EQ(std::wstring(events[1].text), std::wstring(L"after"));
        CHECK(events[1].utterance > events[0].utterance);               /* both helpers called theirs 0 */
    }
}

TEST(Host_StatusGoesBackToStartingWhileTheHelperRestarts)
{
    std::atomic<bool> hold{true};
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int index) {
        if (index == 1) {                                               /* the second one is slow to load */
            while (hold)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello)
                Send(helper, Ready());
            if (m.type == asr::Type::Audio && index == 0)
                return false;                                           /* dies on the first audio */
            return m.type != asr::Type::Stop;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic);
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Live; }));

    const std::vector<std::int16_t> audio = Ramp(160);
    host.Push(StreamId::Microphone, audio.data(), audio.size());
    CHECK(WaitUntil([&] { return helpers.Started() == 2; }));
    CHECK(host.Status(StreamId::Microphone).state == TranscriptState::Starting);
    hold = false;
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Live; }));
    host.Stop();
}

TEST(Host_GivesUpOnAHelperThatKeepsStopping)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &, int) { /* exits at once */ };
    TranscriptionHost::Options options = Quick();
    options.maxRestarts = 2;
    TranscriptionHost host(helpers.Factory(), options);
    host.Start(kMic | kSystem);

    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Failed; }));
    CHECK_EQ(helpers.Started(), std::size_t(3));                        /* the first run and two restarts */
    CHECK(!host.Problem(StreamId::Microphone).empty());
    CHECK(host.Status(StreamId::System).state == TranscriptState::Failed);

    const std::vector<std::int16_t> audio = Ramp(16000);                /* pushing is still harmless */
    host.Push(StreamId::Microphone, audio.data(), audio.size());
    host.Stop();
}

TEST(Host_AHelperThatRanWellHasItsRestartsBack)
{
    FakeHelpers helpers;
    /* Each helper lives 60 ms (its uptime is measured by the host) and then dies; one restart is all that is allowed. */
    helpers.behavior = [&](Instance &helper, int) {
        Send(helper, Ready());
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    };
    TranscriptionHost::Options options = Quick();
    options.maxRestarts = 1;
    options.healthyMs = 30;
    TranscriptionHost host(helpers.Factory(), options);
    host.Start(kMic);

    CHECK(WaitUntil([&] { return helpers.Started() >= 5; }));           /* more than 1 + maxRestarts runs */
    CHECK(host.Status(StreamId::Microphone).state != TranscriptState::Failed);
    host.Stop();
}

TEST(Host_StopDuringASlowStartIsPromptAndNotAFailure)
{
    std::atomic<bool> factoryRunning{false};
    TranscriptionHost host(
        [&](std::wstring *problem, const StopCheck &stopping) -> std::unique_ptr<IAsrTransport> {
            factoryRunning = true;
            for (int i = 0; i < 5000 && !stopping(); ++i)               /* "hashing a model" */
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            *problem = L"given up";
            return nullptr;
        },
        Quick());
    host.Start(kMic);
    CHECK(WaitUntil([&] { return factoryRunning.load(); }));

    const Clock::time_point began = Clock::now();
    host.Stop();
    CHECK(Clock::now() - began < std::chrono::seconds(2));
    CHECK(host.Status(StreamId::Microphone).state == TranscriptState::Off);       /* asked to stop, not failed */
}

TEST(Host_FailsAtOnceWhenThereIsNoHelper)
{
    FakeHelpers helpers;
    helpers.cannotStart = true;
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic);
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Failed; }));
    CHECK_EQ(host.Problem(StreamId::Microphone), std::wstring(L"audior_asr.exe was not found."));

    const std::vector<std::int16_t> audio = Ramp(160);
    host.Push(StreamId::Microphone, audio.data(), audio.size());
    host.Stop();
    CHECK(TakeAll(host, StreamId::Microphone).empty());
}

TEST(Host_ABrokenHelperIsLetGo)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello) {
                const std::uint8_t garbage[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x42, 1, 2, 3};
                helper.out->Write(garbage, sizeof(garbage));            /* a frame bigger than any allowed */
            }
            return true;                                                /* and it carries on listening */
        });
    };
    TranscriptionHost::Options options = Quick();
    options.maxRestarts = 1;
    TranscriptionHost host(helpers.Factory(), options);
    host.Start(kMic);

    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Failed; }));
    CHECK_EQ(helpers.Started(), std::size_t(2));
    host.Stop();
}

TEST(Host_AHelperSendingWhatOnlyTheRecorderSendsIsLetGo)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello) {
                asr::Message stop;
                stop.type = asr::Type::Stop;                            /* not its to say */
                Send(helper, stop);
            }
            return true;
        });
    };
    TranscriptionHost::Options options = Quick();
    options.maxRestarts = 0;
    TranscriptionHost host(helpers.Factory(), options);
    host.Start(kMic);
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Failed; }));
    host.Stop();
}

TEST(Host_AHelperThatNeverGetsReadyIsGivenUpOn)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) { return m.type != asr::Type::Stop; });     /* never says Ready */
    };
    TranscriptionHost::Options options = Quick();
    options.readyTimeoutMs = 60;
    options.maxRestarts = 1;
    TranscriptionHost host(helpers.Factory(), options);
    host.Start(kMic);

    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Failed; }));
    CHECK_EQ(helpers.Started(), std::size_t(2));
    host.Stop();
}

TEST(Host_PushNeverBlocksAndTheNewestAudioSurvives)
{
    std::mutex gate;
    std::condition_variable opened;
    bool open = false;

    FakeHelpers helpers;
    helpers.pipeCapacity = 8 * 1024;                                    /* the helper's pipe fills up quickly */
    helpers.behavior = [&](Instance &helper, int) {
        {
            std::unique_lock<std::mutex> lock(gate);
            opened.wait(lock, [&] { return open; });                    /* busy: reads nothing for now */
        }
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello)
                Send(helper, Ready());
            return m.type != asr::Type::Stop;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic);

    const std::size_t total = 60 * asr::kSampleRate;                    /* a minute of audio, at once */
    const std::vector<std::int16_t> chunk = Ramp(1600);
    const Clock::time_point began = Clock::now();
    for (std::size_t pushed = 0; pushed < total; pushed += chunk.size())
        host.Push(StreamId::Microphone, chunk.data(), chunk.size());
    CHECK(Clock::now() - began < std::chrono::seconds(2));              /* it did not wait for the helper */

    {
        std::lock_guard<std::mutex> lock(gate);
        open = true;
    }
    opened.notify_all();
    host.Stop();

    std::uint64_t received = 0, end = 0;
    for (const Seen &s : helpers.At(0).Seen_()) {
        if (s.type == asr::Type::Audio) {
            received += s.sampleCount;
            end = s.firstSample + s.sampleCount;
        }
    }
    CHECK(received <= 5 * asr::kSampleRate + 8 * 1024);                 /* the queue's five seconds, plus what the pipe held */
    CHECK_EQ(end, std::uint64_t(total));                                /* and they are the newest ones, numbered as pushed */
}

TEST(Host_StopFreesAHelperThatStoppedReading)
{
    FakeHelpers helpers;
    helpers.pipeCapacity = 1024;
    helpers.behavior = [&](Instance &helper, int) {
        while (!helper.in->Closed())                                    /* never reads, until the recorder lets go */
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    };
    TranscriptionHost::Options options = Quick();
    options.flushMs = 50;
    TranscriptionHost host(helpers.Factory(), options);
    host.Start(kMic);

    const std::vector<std::int16_t> audio = Ramp(16000);
    host.Push(StreamId::Microphone, audio.data(), audio.size());
    WaitUntil([&] { return helpers.Started() == 1; });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));        /* the writer is stuck on the full pipe */

    const Clock::time_point began = Clock::now();
    host.Stop();
    CHECK(Clock::now() - began < std::chrono::seconds(4));
}

TEST(Host_TheEventQueueKeepsTheNewest)
{
    FakeHelpers helpers;
    helpers.behavior = [&](Instance &helper, int) {
        Serve(helper, [&](const asr::Message &m) {
            if (m.type == asr::Type::Hello) {
                Send(helper, Ready());
                for (std::uint32_t i = 0; i < 200; ++i)
                    Send(helper, EventFor(StreamId::Microphone, i, true, L"x"));
                Send(helper, StatusFor(StreamId::Microphone, TranscriptState::Live, 0.1f));
            }
            return m.type != asr::Type::Stop;
        });
    };
    TranscriptionHost host(helpers.Factory(), Quick());
    host.Start(kMic);
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).lagSeconds > 0.05f; }));    /* all 200 were sent */
    host.Stop();

    const std::vector<TranscriptEvent> events = TakeAll(host, StreamId::Microphone);
    CHECK(events.size() <= 64 && events.size() > 0);
    CHECK_EQ(events.back().utterance, std::uint32_t(199));              /* the latest is never the one lost */
}

TEST(Host_StartAndStopAreSafeInAnyOrder)
{
    FakeHelpers helpers;
    helpers.behavior = WellBehaved;
    {
        TranscriptionHost host(helpers.Factory(), Quick());
        const std::vector<std::int16_t> audio = Ramp(160);
        host.Push(StreamId::Microphone, audio.data(), audio.size());   /* before Start: ignored */
        host.Stop();                                                    /* before Start: nothing to stop */
        host.Start(0);                                                  /* no streams: nothing starts */
        CHECK_EQ(helpers.Started(), std::size_t(0));
        host.Start(kMic);
        host.Start(kMic | kSystem);                                     /* once only */
        host.Stop();
        host.Stop();
    }
    CHECK_EQ(helpers.Started(), std::size_t(1));
}

TEST(Host_TheDestructorStopsTheHelper)
{
    FakeHelpers helpers;
    helpers.behavior = WellBehaved;
    {
        TranscriptionHost host(helpers.Factory(), Quick());
        host.Start(kMic);
        CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Live; }));
    }
    CHECK_EQ(helpers.At(0).Count(asr::Type::Stop), std::size_t(1));
}

/* ---- a helper that cannot keep up ---- */

/* Says Ready, then that it is hopelessly behind, over and over, until it is let go. */
TEST(Host_ChangesToASmallerModelWhenTheHelperStaysHopelesslyBehind)
{
    FakeHelpers slow, small;
    slow.behavior = [&](Instance &helper, int) {
        Send(helper, Ready());
        while (Send(helper, StatusFor(StreamId::Microphone, TranscriptState::CatchingUp, 9.0f)))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    };
    small.behavior = WellBehaved;

    TranscriptionHost::Options options = Quick();
    options.maxRestarts = 0;                            /* a restart would be the end: this must not use one */
    options.stepDownHoldSeconds = 0.1;
    options.fallbacks.push_back({L"C:\\Models\\smaller.bin", small.Factory()});
    TranscriptionHost host(slow.Factory(), options);
    host.Start(kMic);

    CHECK(WaitUntil([&] { return small.Started() == 1; }));
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Live; }));
    CHECK(host.Status(StreamId::Microphone).smallerModel);
    CHECK_EQ(slow.Started(), std::size_t(1));                           /* the slow one was not started again */

    host.Stop();
    const std::vector<Seen> seen = small.At(0).Seen_();
    CHECK(!seen.empty() && seen.front().type == asr::Type::Hello);
    CHECK_EQ(seen.front().text, std::wstring(L"C:\\Models\\smaller.bin"));
}

TEST(Host_KeepsTheModelWhenThereIsNoSmallerOne)
{
    FakeHelpers slow;
    slow.behavior = [&](Instance &helper, int) {
        Send(helper, Ready());
        for (int i = 0; i < 60 && Send(helper, StatusFor(StreamId::Microphone, TranscriptState::CatchingUp, 9.0f)); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        Serve(helper, [&](const asr::Message &m) { return m.type != asr::Type::Stop; });
    };
    TranscriptionHost::Options options = Quick();
    options.stepDownHoldSeconds = 0.05;
    TranscriptionHost host(slow.Factory(), options);
    host.Start(kMic);

    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::CatchingUp; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));        /* long past the hold time */
    CHECK_EQ(slow.Started(), std::size_t(1));
    CHECK(!host.Status(StreamId::Microphone).smallerModel);
    host.Stop();
}

TEST(Host_ALagThatRecoversChangesNothing)
{
    FakeHelpers helpers;
    TranscriptionHost::Options options = Quick();
    options.stepDownHoldSeconds = 0.15;
    FakeHelpers small;
    small.behavior = WellBehaved;
    options.fallbacks.push_back({L"C:\\Models\\smaller.bin", small.Factory()});
    helpers.behavior = [&](Instance &helper, int) {
        Send(helper, Ready());
        for (int i = 0; i < 30; ++i) {                  /* behind for one beat (10 ms, or what the timer gives: 16), then fine, again and again;
                                                           a bad stretch is a tenth of the hold time, so a slow runner does not make it a fall */
            const float lag = i % 2 == 0 ? 9.0f : 0.5f;
            if (!Send(helper, StatusFor(StreamId::Microphone, TranscriptState::Live, lag)))
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Serve(helper, [&](const asr::Message &m) { return m.type != asr::Type::Stop; });
    };
    TranscriptionHost host(helpers.Factory(), options);
    host.Start(kMic);
    CHECK(WaitUntil([&] { return helpers.Started() == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    CHECK_EQ(small.Started(), std::size_t(0));
    host.Stop();
}
