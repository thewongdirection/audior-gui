/* Tests of the recording session, using generated audio and in-memory files. */
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <vector>

#include "../src/engine/recording_session.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

/* ---- a capture source that makes up audio ---- */

struct Counters {
    std::atomic<std::uint64_t> frames{0};       /* frames handed to the session */
    std::atomic<int>           polls{0};
    std::atomic<bool>          stopped{false};
};

struct SourceSpec {
    std::int16_t level          = 1000;         /* every sample                              */
    HRESULT      openHr         = S_OK;
    int          failAfterPolls = -1;           /* Poll() fails after this many; -1 = never  */
    HRESULT      failHr         = S_OK;
    std::size_t  framesPerPoll  = 480;          /* 10 ms of audio: real time at a 10 ms poll */
};

class FakeSource final : public ICaptureSource {
public:
    FakeSource(const SourceSpec &spec, std::shared_ptr<Counters> counters)
        : spec_(spec), counters_(std::move(counters)) {}

    HRESULT Open() override { return spec_.openHr; }
    HRESULT Start() override { return S_OK; }
    HRESULT Poll(std::vector<std::int16_t> *frames) override
    {
        const int n = ++counters_->polls;
        if (spec_.failAfterPolls >= 0 && n > spec_.failAfterPolls)
            return spec_.failHr;
        frames->insert(frames->end(), spec_.framesPerPoll * kPcmChannels, spec_.level);
        counters_->frames += spec_.framesPerPoll;
        return S_OK;
    }
    void Stop() override { counters_->stopped = true; }

private:
    SourceSpec                spec_;
    std::shared_ptr<Counters> counters_;
};

/* ---- a sink that keeps what it is given in memory ---- */

struct SinkSpec {
    std::uint64_t        failAfterBytes = ~0ull;
    HRESULT              writeHr        = HRESULT_FROM_WIN32(ERROR_DISK_FULL);
    HRESULT              closeHr        = S_OK;
    std::atomic<bool>   *gate           = nullptr;      /* writes wait until this is true */
};

/* What a sink recorded.  The session owns (and deletes) its sinks, so tests read this instead. */
struct SinkState {
    std::vector<std::int16_t>  samples;
    bool                       closed = false;
    bool                       kept   = false;
    std::atomic<std::uint64_t> bytes{0};

    std::uint64_t BytesWritten() const { return bytes; }
};

class MemorySink final : public IAudioSink {
public:
    MemorySink(const SinkSpec &spec, std::shared_ptr<SinkState> state) : spec_(spec), state_(std::move(state)) {}

    HRESULT Write(const void *pcm, std::size_t bytes) override
    {
        for (int i = 0; spec_.gate && !spec_.gate->load() && i < 10000; ++i)
            Sleep(1);
        if (state_->bytes + bytes > spec_.failAfterBytes)
            return spec_.writeHr;
        const std::int16_t *p = static_cast<const std::int16_t *>(pcm);
        state_->samples.insert(state_->samples.end(), p, p + bytes / sizeof(std::int16_t));
        state_->bytes += bytes;
        return S_OK;
    }
    HRESULT Close(bool keep) override
    {
        state_->closed = true;
        state_->kept = keep;
        return spec_.closeHr;
    }
    std::uint64_t BytesWritten() const override { return state_->bytes; }

private:
    SinkSpec                   spec_;
    std::shared_ptr<SinkState> state_;
};

/* ---- the test rig ---- */

struct Rig {
    SourceSpec                sources[kStreamCount];
    std::shared_ptr<Counters> counters[kStreamCount];
    SinkSpec                  sinkSpec;
    int                       failFileIndex = -1;       /* the sink that cannot be created */
    HRESULT                   sinkOpenHr    = HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
    std::vector<std::shared_ptr<SinkState>> sinks;      /* one per file created */

    Rig()
    {
        for (auto &c : counters)
            c = std::make_shared<Counters>();
        sources[StreamIndex(StreamId::System)].level = 3000;
    }

    RecordingSession::SourceFactory Sources()
    {
        return [this](StreamId id) -> std::unique_ptr<ICaptureSource> {
            return std::make_unique<FakeSource>(sources[StreamIndex(id)], counters[StreamIndex(id)]);
        };
    }

    RecordingSession::SinkFactory Sinks()
    {
        return [this](const std::wstring &, HRESULT *hr) -> std::unique_ptr<IAudioSink> {
            if (static_cast<int>(sinks.size()) == failFileIndex) {
                *hr = sinkOpenHr;
                return nullptr;
            }
            sinks.push_back(std::make_shared<SinkState>());
            *hr = S_OK;
            return std::make_unique<MemorySink>(sinkSpec, sinks.back());
        };
    }

    std::uint64_t Frames(StreamId id) const { return counters[StreamIndex(id)]->frames; }
    int           Polls(StreamId id) const { return counters[StreamIndex(id)]->polls; }
};

template <typename Pred>
bool WaitUntil(Pred done, DWORD timeoutMs = 5000)
{
    for (DWORD waited = 0; waited < timeoutMs; waited += 2) {
        if (done())
            return true;
        Sleep(2);
    }
    return done();
}

SessionPlan Plan(bool withMic, bool withSystem, bool merge, float micWeight = 0.5f)
{
    SessionPlan plan;
    plan.microphone = withMic;
    plan.system = withSystem;
    plan.merge = merge;
    plan.micWeight = micWeight;
    if (withMic && withSystem && !merge)
        plan.files = {L"C:\\rec\\a_mic.wav", L"C:\\rec\\a_system.wav"};
    else
        plan.files = {L"C:\\rec\\a.wav"};
    return plan;
}

bool AllEqual(const std::vector<std::int16_t> &v, std::size_t from, std::size_t to, int expected, int tolerance = 0)
{
    for (std::size_t i = from; i < to; ++i) {
        if (std::abs(static_cast<int>(v[i]) - expected) > tolerance)
            return false;
    }
    return true;
}

/* Records for a moment (about 15 polls of each stream), then stops. */
void RecordBriefly(RecordingSession &session, Rig &rig, bool withMic, bool withSystem)
{
    CHECK(WaitUntil([&] {
        return (!withMic || rig.Polls(StreamId::Microphone) >= 15) &&
               (!withSystem || rig.Polls(StreamId::System) >= 15);
    }));
    session.Stop();
}

}  // namespace

/* ---- the plan ---- */

TEST(MakePlan_FollowsTheSettings)
{
    RecordingSettings s;
    s.includeSystemAudio = true;
    s.mergeMicPercent = 80;
    SessionPlan plan = MakePlan(s, L"C:\\r\\x.mp3");
    CHECK(plan.microphone && plan.system && plan.merge);
    CHECK_NEAR(plan.micWeight, 0.8, 1e-6);
    CHECK_EQ(plan.files.size(), std::size_t(1));

    s.mixMode = MixMode::Separate;
    plan = MakePlan(s, L"C:\\r\\x.mp3");
    CHECK(!plan.merge);
    CHECK_EQ(plan.files.size(), std::size_t(2));
    CHECK_EQ(plan.files[0], std::wstring(L"C:\\r\\x_mic.mp3"));

    s.includeMicrophone = false;                    /* one stream: nothing to merge or separate */
    plan = MakePlan(s, L"C:\\r\\x.mp3");
    CHECK(!plan.microphone && plan.system && !plan.merge);
    CHECK_EQ(plan.files.size(), std::size_t(1));
}

/* ---- recording ---- */

TEST(Session_MergesBothStreamsAtTheChosenShare)
{
    Rig rig;                                        /* microphone 1000, system 3000 */
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, true, true, 0.5f), &error));
    CHECK(error.empty());
    CHECK(session.Running());
    RecordBriefly(session, rig, true, true);

    CHECK(!session.Running());
    CHECK(session.Failure().empty());
    CHECK_EQ(rig.sinks.size(), std::size_t(1));
    const SinkState &file = *rig.sinks[0];
    CHECK(file.closed && file.kept);

    const std::uint64_t mic = rig.Frames(StreamId::Microphone), sys = rig.Frames(StreamId::System);
    const std::size_t shortest = static_cast<std::size_t>(std::min(mic, sys));
    const std::size_t longest = static_cast<std::size_t>(std::max(mic, sys));
    CHECK_EQ(file.samples.size(), longest * kPcmChannels);          /* nothing captured is lost  */
    CHECK(AllEqual(file.samples, 0, shortest * kPcmChannels, 2000, 1));     /* (1000 + 3000) / 2 */
    /* Past the end of the shorter stream only the longer one is left, silence-padded. */
    CHECK(AllEqual(file.samples, shortest * kPcmChannels, longest * kPcmChannels, mic > sys ? 500 : 1500, 1));
}

TEST(Session_MergeWeightsFollowTheMicrophoneShare)
{
    Rig rig;
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, true, true, 0.8f), &error));
    RecordBriefly(session, rig, true, true);

    const std::size_t shortest = static_cast<std::size_t>(std::min(rig.Frames(StreamId::Microphone),
                                                                    rig.Frames(StreamId::System)));
    CHECK(AllEqual(rig.sinks[0]->samples, 0, shortest * kPcmChannels, 1400, 1));   /* 1000*.8 + 3000*.2 */
}

TEST(Session_SeparateTracksKeepEachStreamIntact)
{
    Rig rig;
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, true, false), &error));
    RecordBriefly(session, rig, true, true);

    CHECK(session.Failure().empty());
    CHECK_EQ(rig.sinks.size(), std::size_t(2));
    const SinkState &mic = *rig.sinks[0], &sys = *rig.sinks[1];
    CHECK_EQ(mic.samples.size(), static_cast<std::size_t>(rig.Frames(StreamId::Microphone)) * kPcmChannels);
    CHECK_EQ(sys.samples.size(), static_cast<std::size_t>(rig.Frames(StreamId::System)) * kPcmChannels);
    CHECK(AllEqual(mic.samples, 0, mic.samples.size(), 1000));
    CHECK(AllEqual(sys.samples, 0, sys.samples.size(), 3000));
    CHECK(mic.kept && sys.kept);
}

TEST(Session_RecordsOneStreamAlone)
{
    {
        Rig rig;
        RecordingSession session(rig.Sources(), rig.Sinks());
        std::wstring error;
        CHECK(session.Start(Plan(true, false, false), &error));
        RecordBriefly(session, rig, true, false);
        CHECK_EQ(rig.sinks.size(), std::size_t(1));
        CHECK(AllEqual(rig.sinks[0]->samples, 0, rig.sinks[0]->samples.size(), 1000));
        CHECK_EQ(rig.Polls(StreamId::System), 0);               /* the other stream was never opened */
        PeakBin bin[4];
        CHECK_EQ(session.TakePeaks(StreamId::System, bin, 4), std::size_t(0));
    }
    {
        Rig rig;
        RecordingSession session(rig.Sources(), rig.Sinks());
        std::wstring error;
        CHECK(session.Start(Plan(false, true, false), &error));
        RecordBriefly(session, rig, false, true);
        CHECK_EQ(rig.sinks.size(), std::size_t(1));
        CHECK(AllEqual(rig.sinks[0]->samples, 0, rig.sinks[0]->samples.size(), 3000));
        CHECK_EQ(rig.Polls(StreamId::Microphone), 0);
    }
}

TEST(Session_ReportsEachStreamsPeaksBeforeMerging)
{
    Rig rig;
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, true, true, 0.5f), &error));

    PeakBin mic[64], sys[64];
    std::size_t micCount = 0, sysCount = 0;
    CHECK(WaitUntil([&] {
        micCount += session.TakePeaks(StreamId::Microphone, mic, 1);
        sysCount += session.TakePeaks(StreamId::System, sys, 1);
        return micCount >= 3 && sysCount >= 3;
    }));
    session.Stop();

    /* Each lane shows its own stream, not the mix. */
    CHECK_NEAR(mic[0].high, 1000.0 / 32768.0, 1e-5);
    CHECK_NEAR(sys[0].high, 3000.0 / 32768.0, 1e-5);
}

TEST(Session_StatsCountWhatWasWritten)
{
    Rig rig;
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, false, false), &error));
    RecordBriefly(session, rig, true, false);

    const RecordingStats stats = session.Stats();
    CHECK_NEAR(stats.seconds, static_cast<double>(rig.Frames(StreamId::Microphone)) / 48000.0, 1e-6);
    CHECK_EQ(stats.bytesWritten, rig.Frames(StreamId::Microphone) * kPcmFrameBytes);
    CHECK(stats.seconds > 0.1);
}

TEST(Session_FilesListsWhatItWrites)
{
    Rig rig;
    RecordingSession session(rig.Sources(), rig.Sinks());
    CHECK(session.Files().empty());                 /* nothing yet */
    std::wstring error;
    CHECK(session.Start(Plan(true, true, false), &error));
    CHECK_EQ(session.Files().size(), std::size_t(2));
    session.Stop();
}

/* ---- starting goes wrong ---- */

TEST(Session_AFailingMicrophoneStopsTheStartAndLeavesNothing)
{
    Rig rig;
    rig.sources[StreamIndex(StreamId::Microphone)].openHr = static_cast<HRESULT>(0x8889000A);   /* in use */
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(!session.Start(Plan(true, true, true), &error));

    CHECK(error.find(L"microphone") != std::wstring::npos);
    CHECK(error.find(L"another program") != std::wstring::npos);
    CHECK(rig.sinks.empty());                       /* no file was created */
    CHECK(rig.counters[StreamIndex(StreamId::System)]->stopped);        /* the other device was released */
    CHECK(rig.counters[StreamIndex(StreamId::Microphone)]->stopped);
    CHECK(!session.Running());
}

TEST(Session_AFailingPlaybackDeviceIsReportedAsSuch)
{
    Rig rig;
    rig.sources[StreamIndex(StreamId::System)].openHr = static_cast<HRESULT>(0x80070490);
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(!session.Start(Plan(true, true, true), &error));
    CHECK(error.find(L"playback device") != std::wstring::npos);
    CHECK(rig.sinks.empty());
}

TEST(Session_AFileThatCannotBeCreatedRemovesTheOthers)
{
    Rig rig;
    rig.failFileIndex = 1;                          /* the second of the two files */
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(!session.Start(Plan(true, true, false), &error));

    CHECK(error.find(L"a_system.wav") != std::wstring::npos);
    CHECK(error.find(L"already exists") != std::wstring::npos);
    CHECK_EQ(rig.sinks.size(), std::size_t(1));
    CHECK(rig.sinks[0]->closed && !rig.sinks[0]->kept);                 /* the first one was discarded */
    CHECK(rig.counters[StreamIndex(StreamId::Microphone)]->stopped);    /* devices released */
    CHECK(rig.counters[StreamIndex(StreamId::System)]->stopped);
}

TEST(Session_RefusesAnEmptyOrMismatchedPlan)
{
    Rig rig;
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(!session.Start(Plan(false, false, false), &error));           /* no stream */
    SessionPlan twoFilesForOneStream = Plan(true, false, false);
    twoFilesForOneStream.files.push_back(L"C:\\rec\\b.wav");
    RecordingSession other(rig.Sources(), rig.Sinks());
    CHECK(!other.Start(twoFilesForOneStream, &error));
    CHECK(rig.sinks.empty());
}

TEST(Session_RecordsOnlyOnce)
{
    Rig rig;
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, false, false), &error));
    CHECK(!session.Start(Plan(true, false, false), &error));
    session.Stop();
    CHECK(!session.Start(Plan(true, false, false), &error));
}

/* ---- failures while recording ---- */

TEST(Session_ADeviceThatVanishesEndsTheRecordingWithAValidFile)
{
    Rig rig;
    SourceSpec &mic = rig.sources[StreamIndex(StreamId::Microphone)];
    mic.failAfterPolls = 8;
    mic.failHr = static_cast<HRESULT>(0x88890004);                      /* device invalidated */
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, false, false), &error));

    CHECK(WaitUntil([&] { return !session.Running(); }));               /* ends by itself */
    const std::wstring failure = session.Failure();
    CHECK(failure.find(L"Audio capture failed") != std::wstring::npos);
    CHECK(failure.find(L"unplugged") != std::wstring::npos);

    session.Stop();                                                     /* what the controller does next */
    CHECK_EQ(rig.sinks.size(), std::size_t(1));
    CHECK(rig.sinks[0]->closed && rig.sinks[0]->kept);
    CHECK_EQ(rig.sinks[0]->samples.size(), std::size_t(8 * 480 * 2));   /* what came before the failure */
    CHECK(rig.counters[StreamIndex(StreamId::Microphone)]->stopped);    /* device released */
}

TEST(Session_ADeviceFailureStopsTheOtherStreamToo)
{
    Rig rig;
    SourceSpec &sys = rig.sources[StreamIndex(StreamId::System)];
    sys.failAfterPolls = 5;
    sys.failHr = static_cast<HRESULT>(0x88890004);
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, true, false), &error));

    CHECK(WaitUntil([&] { return !session.Running(); }));
    session.Stop();
    CHECK(rig.counters[StreamIndex(StreamId::Microphone)]->stopped);
    CHECK(rig.counters[StreamIndex(StreamId::System)]->stopped);
    CHECK_EQ(rig.sinks.size(), std::size_t(2));
    CHECK(rig.sinks[0]->kept && rig.sinks[1]->kept);
}

TEST(Session_AFullDiskEndsTheRecordingAndKeepsWhatWasWritten)
{
    Rig rig;
    rig.sinkSpec.failAfterBytes = 20000;                                /* the "disk" fills up */
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, false, false), &error));

    CHECK(WaitUntil([&] { return !session.Running(); }));
    session.Stop();
    const std::wstring failure = session.Failure();
    CHECK(failure.find(L"Failed to write audio data") != std::wstring::npos);
    CHECK(failure.find(L"disk is full") != std::wstring::npos);
    CHECK(rig.sinks[0]->closed && rig.sinks[0]->kept);                  /* finalised, not discarded */
    CHECK(rig.sinks[0]->BytesWritten() <= 20000);
}

TEST(Session_AFailureToFinaliseIsReported)
{
    Rig rig;
    rig.sinkSpec.closeHr = HRESULT_FROM_WIN32(ERROR_DISK_FULL);
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, false, false), &error));
    RecordBriefly(session, rig, true, false);
    CHECK(session.Failure().find(L"Failed to finalise output file") != std::wstring::npos);
}

TEST(Session_AWriterThatCannotKeepUpEndsTheRecordingInsteadOfGrowing)
{
    Rig rig;
    std::atomic<bool> release{false};
    rig.sinkSpec.gate = &release;                                       /* the writer is stuck */
    rig.sources[StreamIndex(StreamId::Microphone)].framesPerPoll = 48000;   /* a second of audio per poll */
    RecordingSession session(rig.Sources(), rig.Sinks());
    std::wstring error;
    CHECK(session.Start(Plan(true, false, false), &error));

    CHECK(WaitUntil([&] { return !session.Running(); }));               /* the 8 s ring overflowed */
    CHECK(session.Failure().find(L"fell behind") != std::wstring::npos);
    release = true;
    session.Stop();
    CHECK(rig.sinks[0]->kept);
    /* The audio the ring held was written; no more than its capacity was ever buffered. */
    CHECK(rig.sinks[0]->samples.size() <= static_cast<std::size_t>(9 * kPcmRate) * kPcmChannels);
}

/* ---- stopping ---- */

TEST(Session_AnotherProcessCanStopIt)
{
    HANDLE external = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Rig rig;
    RecordingSession session(rig.Sources(), rig.Sinks(), external);
    std::wstring error;
    CHECK(session.Start(Plan(true, false, false), &error));
    CHECK(WaitUntil([&] { return rig.Polls(StreamId::Microphone) >= 5; }));
    CHECK(session.Running());

    SetEvent(external);
    CHECK(WaitUntil([&] { return !session.Running(); }));
    session.Stop();
    CHECK(session.Failure().empty());                                   /* a normal end */
    CHECK(rig.sinks[0]->closed && rig.sinks[0]->kept);
    CHECK(!rig.sinks[0]->samples.empty());
    CloseHandle(external);
}

TEST(Session_StopIsRepeatableAndHarmlessBeforeStart)
{
    Rig rig;
    RecordingSession session(rig.Sources(), rig.Sinks());
    session.Stop();                                                     /* nothing running */
    std::wstring error;
    CHECK(session.Start(Plan(true, false, false), &error));
    session.Stop();
    session.Stop();
    CHECK(!session.Running());
}

TEST(Session_DestroyingARunningSessionFinishesTheFiles)
{
    Rig rig;
    {
        RecordingSession session(rig.Sources(), rig.Sinks());
        std::wstring error;
        CHECK(session.Start(Plan(true, false, false), &error));
        CHECK(WaitUntil([&] { return rig.Polls(StreamId::Microphone) >= 3; }));
    }                                                                   /* destroyed while recording */
    CHECK_EQ(rig.sinks.size(), std::size_t(1));
    CHECK(rig.sinks[0]->closed && rig.sinks[0]->kept);                  /* finished, not discarded */
    CHECK(!rig.sinks[0]->samples.empty());
    CHECK(rig.counters[StreamIndex(StreamId::Microphone)]->stopped);
}
