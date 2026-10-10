#include "../src/core/recorder_controller.h"
#include "../src/engine/simulated_engine.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

/* A controller on a simulated engine, with a clock the test moves by hand. */
struct Rig {
    double    now = 0.0;
    LocalTime time;
    SimulatedEngine engine;
    RecorderController ctl;

    Rig()
        : engine([this] { return now; }),
          ctl(engine, [this] { return time; })
    {
        time.year = 2026; time.month = 10; time.day = 10;
        time.hour = 15; time.minute = 30; time.second = 45;
        ctl.Settings().outputDirectory = L"C:\\Rec";
    }

    /* Let `seconds` pass, ticking the way the UI does (60 times a second). */
    void Run(double seconds)
    {
        const double step = 1.0 / 60.0;
        for (double t = 0.0; t < seconds - 1e-9; t += step) {
            now += step;
            ctl.Tick(step);
        }
    }
};

/* An engine that refuses to start. */
class RefusingEngine : public IRecordingEngine {
public:
    bool IsSimulated() const override { return false; }
    std::vector<DeviceInfo> ListDevices(DeviceKind) override { return {}; }
    bool Start(const RecordingSettings &, const std::wstring &, std::wstring *error) override
    {
        *error = L"Could not open microphone device.";
        return false;
    }
    void Stop() override {}
    bool IsRecording() override { return false; }
    RecordingStats Stats() override { return RecordingStats(); }
    std::size_t TakePeaks(StreamId, PeakBin *, std::size_t) override { return 0; }
    std::wstring Failure() override { return L""; }
    std::vector<std::wstring> OutputFiles() override { return {}; }
};

}  // namespace

TEST(Controller_StartsIdleWithDevicesListed)
{
    Rig rig;
    CHECK(rig.ctl.GetPhase() == Phase::Idle);
    CHECK(rig.ctl.CanStart());
    CHECK(!rig.ctl.CanStop());
    CHECK(!rig.ctl.SettingsLocked());
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::None);
    CHECK(!rig.ctl.Devices(DeviceKind::Microphone).empty());
    CHECK(!rig.ctl.Devices(DeviceKind::Playback).empty());
    CHECK(rig.ctl.IsSimulated());
}

TEST(Controller_StartNeedsAnOutputFolder)
{
    Rig rig;
    rig.ctl.Settings().outputDirectory.clear();
    CHECK(!rig.ctl.Start());
    CHECK(rig.ctl.GetPhase() == Phase::Idle);
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::Failed);
    CHECK(!rig.ctl.LastOutcome().message.empty());
}

TEST(Controller_RecordsAndSaves)
{
    Rig rig;
    CHECK(rig.ctl.Start());
    CHECK(rig.ctl.GetPhase() == Phase::Recording);
    CHECK(!rig.ctl.CanStart());
    CHECK(rig.ctl.CanStop());
    CHECK(rig.ctl.SettingsLocked());

    rig.Run(2.0);
    CHECK_NEAR(rig.ctl.Stats().seconds, 2.0, 0.05);
    CHECK(rig.ctl.Stats().bytesWritten > 0);

    rig.ctl.Stop();
    CHECK(rig.ctl.GetPhase() == Phase::Idle);
    CHECK(rig.ctl.CanStart());
    const Outcome &o = rig.ctl.LastOutcome();
    CHECK(o.kind == Outcome::Kind::Saved);
    CHECK_EQ(o.files.size(), std::size_t(1));
    CHECK_EQ(o.files[0], std::wstring(L"C:\\Rec\\audior-20261010-153045.mp3"));
    CHECK_NEAR(rig.ctl.Stats().seconds, 2.0, 0.05);     /* the final figures stay */
}

TEST(Controller_WavRecordingsGetAWavName)
{
    Rig rig;
    rig.ctl.Settings().format = Format::Wav;
    CHECK(rig.ctl.Start());
    rig.ctl.Stop();
    CHECK_EQ(rig.ctl.LastOutcome().files[0], std::wstring(L"C:\\Rec\\audior-20261010-153045.wav"));
}

TEST(Controller_SeparateStreamsGiveTwoFiles)
{
    Rig rig;
    rig.ctl.Settings().includeSystemAudio = true;
    rig.ctl.Settings().mixMode = MixMode::Separate;
    CHECK(rig.ctl.Start());
    rig.ctl.Stop();
    const auto &files = rig.ctl.LastOutcome().files;
    CHECK_EQ(files.size(), std::size_t(2));
    CHECK_EQ(files[0], std::wstring(L"C:\\Rec\\audior-20261010-153045_mic.mp3"));
    CHECK_EQ(files[1], std::wstring(L"C:\\Rec\\audior-20261010-153045_system.mp3"));
}

TEST(Controller_OnlyTheMicLaneRunsWithoutSystemAudio)
{
    Rig rig;
    rig.ctl.Start();
    rig.Run(1.0);
    CHECK(rig.ctl.StreamEnabled(StreamId::Microphone));
    CHECK(!rig.ctl.StreamEnabled(StreamId::System));
    CHECK(rig.ctl.Lane(StreamId::Microphone).HasAudio());
    CHECK(!rig.ctl.Lane(StreamId::System).HasAudio());
}

TEST(Controller_BothLanesRunWithSystemAudio)
{
    Rig rig;
    rig.ctl.Settings().includeSystemAudio = true;
    rig.ctl.Start();
    rig.Run(1.0);
    CHECK(rig.ctl.StreamEnabled(StreamId::System));
    CHECK(rig.ctl.Lane(StreamId::Microphone).HasAudio());
    CHECK(rig.ctl.Lane(StreamId::System).HasAudio());
}

TEST(Controller_LanesPreviewTheDraftSettingsWhileIdle)
{
    Rig rig;
    CHECK(!rig.ctl.StreamEnabled(StreamId::System));
    rig.ctl.Settings().includeSystemAudio = true;
    CHECK(rig.ctl.StreamEnabled(StreamId::System));
}

TEST(Controller_RecordingKeepsTheSettingsItStartedWith)
{
    Rig rig;
    rig.ctl.Settings().includeSystemAudio = true;
    rig.ctl.Start();

    rig.ctl.Settings().includeSystemAudio = false;      /* the UI locks this; be safe anyway */
    CHECK(rig.ctl.StreamEnabled(StreamId::System));
    CHECK(rig.ctl.Active().includeSystemAudio);

    rig.ctl.Stop();
    CHECK(!rig.ctl.StreamEnabled(StreamId::System));    /* back to the draft */
}

TEST(Controller_WaveformsAdvanceWithTime)
{
    Rig rig;
    rig.ctl.Start();
    rig.Run(2.0);
    const double head = rig.ctl.Lane(StreamId::Microphone).Playhead();
    CHECK(head > 150.0 && head <= 200.0);               /* ~2 s of 100 bins/s */
}

TEST(Controller_StartingTwiceAndStoppingWhenIdleAreIgnored)
{
    Rig rig;
    rig.ctl.Stop();                                     /* idle: nothing to stop */
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::None);

    CHECK(rig.ctl.Start());
    CHECK(!rig.ctl.Start());                            /* already recording */
    CHECK(rig.ctl.GetPhase() == Phase::Recording);
}

TEST(Controller_ANewRecordingStartsWithAFreshWaveform)
{
    Rig rig;
    rig.ctl.Start();
    rig.Run(2.0);
    rig.ctl.Stop();
    CHECK(rig.ctl.Lane(StreamId::Microphone).HasAudio());

    rig.time.second = 50;
    rig.ctl.Start();
    CHECK(!rig.ctl.Lane(StreamId::Microphone).HasAudio());
    CHECK_NEAR(rig.ctl.Stats().seconds, 0.0, 0.0);
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::None);
}

TEST(Controller_AnEngineFailureEndsTheRecordingWithAnError)
{
    Rig rig;
    rig.engine.FailAfter(1.5, L"Audio capture failed (hr=0x88890004)");
    rig.ctl.Start();
    rig.Run(3.0);

    CHECK(rig.ctl.GetPhase() == Phase::Idle);
    const Outcome &o = rig.ctl.LastOutcome();
    CHECK(o.kind == Outcome::Kind::Failed);
    CHECK_EQ(o.message, std::wstring(L"Audio capture failed (hr=0x88890004)"));
    CHECK_EQ(o.files.size(), std::size_t(1));           /* what was captured is kept */
    CHECK_NEAR(rig.ctl.Stats().seconds, 1.5, 0.05);
}

TEST(Controller_AnEngineThatCannotStartLeavesTheReason)
{
    RefusingEngine engine;
    RecorderController ctl(engine, [] { return LocalTime(); });
    ctl.Settings().outputDirectory = L"C:\\Rec";

    CHECK(!ctl.Start());
    CHECK(ctl.GetPhase() == Phase::Idle);
    CHECK(ctl.LastOutcome().kind == Outcome::Kind::Failed);
    CHECK_EQ(ctl.LastOutcome().message, std::wstring(L"Could not open microphone device."));
    CHECK(!ctl.SettingsLocked());
}

TEST(Controller_RestartingWithinASecondDoesNotOverwrite)
{
    Rig rig;
    std::vector<std::wstring> names;
    for (int i = 0; i < 3; ++i) {
        CHECK(rig.ctl.Start());
        rig.ctl.Stop();
        names.push_back(rig.ctl.LastOutcome().files[0]);
    }
    CHECK_EQ(names[0], std::wstring(L"C:\\Rec\\audior-20261010-153045.mp3"));
    CHECK_EQ(names[1], std::wstring(L"C:\\Rec\\audior-20261010-153045-2.mp3"));
    CHECK_EQ(names[2], std::wstring(L"C:\\Rec\\audior-20261010-153045-3.mp3"));

    rig.time.second = 46;                               /* a new second: a plain name again */
    rig.ctl.Start();
    rig.ctl.Stop();
    CHECK_EQ(rig.ctl.LastOutcome().files[0], std::wstring(L"C:\\Rec\\audior-20261010-153046.mp3"));
}

TEST(Controller_AFailedStartDoesNotUseUpAName)
{
    Rig rig;
    rig.ctl.Settings().mergeMicPercent = 500;           /* invalid */
    CHECK(!rig.ctl.Start());
    rig.ctl.Settings().mergeMicPercent = 65;
    CHECK(rig.ctl.Start());
    rig.ctl.Stop();
    CHECK_EQ(rig.ctl.LastOutcome().files[0], std::wstring(L"C:\\Rec\\audior-20261010-153045.mp3"));
}

TEST(Controller_TickWhileIdleIsHarmless)
{
    Rig rig;
    rig.Run(1.0);
    CHECK(rig.ctl.GetPhase() == Phase::Idle);
    CHECK(!rig.ctl.Lane(StreamId::Microphone).HasAudio());
}

TEST(Controller_ALongGapBetweenTicksStillDeliversTheAudio)
{
    Rig rig;                                            /* e.g. the window was dragged */
    rig.ctl.Start();
    rig.now = 30.0;
    rig.ctl.Tick(30.0);
    CHECK(rig.ctl.Lane(StreamId::Microphone).HasAudio());
    CHECK_NEAR(rig.ctl.Stats().seconds, 30.0, 1e-9);
}

TEST(Controller_AnimatesWhileRecordingAndUntilTheWaveformsSettle)
{
    Rig rig;
    CHECK(!rig.ctl.Animating());            /* idle and quiet */

    rig.ctl.Start();
    CHECK(rig.ctl.Animating());
    rig.Run(2.0);
    CHECK(rig.ctl.Animating());

    rig.ctl.Stop();
    CHECK(rig.ctl.Animating());             /* the waveform is still catching up and the meter falling */

    rig.Run(10.0);
    CHECK(!rig.ctl.Animating());            /* settled: the UI can stop its timer */
}

namespace {

/* Counts Stop() calls. */
class SpyEngine : public SimulatedEngine {
public:
    using SimulatedEngine::SimulatedEngine;
    int stops = 0;
    void Stop() override
    {
        ++stops;
        SimulatedEngine::Stop();
    }
};

}  // namespace

TEST(Controller_AcknowledgesAFailedRecordingWithStop)
{
    double now = 0.0;
    SpyEngine engine([&now] { return now; });
    RecorderController ctl(engine, [] { return LocalTime(); });
    ctl.Settings().outputDirectory = L"C:\\Rec";
    engine.FailAfter(1.0, L"Audio capture failed");

    ctl.Start();
    now = 2.0;
    ctl.Tick(2.0);
    CHECK(ctl.GetPhase() == Phase::Idle);
    CHECK_EQ(engine.stops, 1);                          /* the engine was told to let go */
    CHECK(ctl.LastOutcome().kind == Outcome::Kind::Failed);
}
