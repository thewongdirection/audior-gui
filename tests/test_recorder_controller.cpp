#include <set>

#include "../src/core/recorder_controller.h"
#include "../src/engine/simulated_engine.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

/* A controller on a simulated engine, with a clock the test moves by hand. */
struct Rig {
    double    now = 0.0;
    LocalTime time;
    std::set<std::wstring> existing;                    /* the "disk": files earlier recordings made */
    SimulatedEngine engine;
    RecorderController ctl;

    Rig()
        : engine([this] { return now; }),
          ctl(engine, [this] { return time; },
              [this](const std::wstring &path) { return existing.count(path) > 0; })
    {
        time.year = 2026; time.month = 10; time.day = 10;
        time.hour = 15; time.minute = 30; time.second = 45;
        ctl.Settings().outputDirectory = L"C:\\Rec";
    }

    /* Records for no time at all; the files it "wrote" now exist.  Returns the first. */
    std::wstring Record()
    {
        CHECK(ctl.Start());
        ctl.Stop();
        const std::vector<std::wstring> &files = ctl.LastOutcome().files;
        existing.insert(files.begin(), files.end());
        return files.empty() ? std::wstring() : files[0];
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
    CHECK_EQ(o.files[0], std::wstring(L"C:\\Rec\\Audior-GUI-20261010-153045.mp3"));
    CHECK_NEAR(rig.ctl.Stats().seconds, 2.0, 0.05);     /* the final figures stay */
}

TEST(Controller_WavRecordingsGetAWavName)
{
    Rig rig;
    rig.ctl.Settings().format = Format::Wav;
    CHECK(rig.ctl.Start());
    rig.ctl.Stop();
    CHECK_EQ(rig.ctl.LastOutcome().files[0], std::wstring(L"C:\\Rec\\Audior-GUI-20261010-153045.wav"));
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
    CHECK_EQ(files[0], std::wstring(L"C:\\Rec\\Audior-GUI-20261010-153045_mic.mp3"));
    CHECK_EQ(files[1], std::wstring(L"C:\\Rec\\Audior-GUI-20261010-153045_system.mp3"));
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
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\Audior-GUI-20261010-153045.mp3"));
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\Audior-GUI-20261010-153045 (2).mp3"));
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\Audior-GUI-20261010-153045 (3).mp3"));

    rig.time.second = 46;                               /* a new second: a plain name again */
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\Audior-GUI-20261010-153046.mp3"));
}

TEST(Controller_ATypedNameGetsTheFormatsExtension)
{
    Rig rig;
    rig.ctl.Settings().fileName = L"meeting";
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\meeting.mp3"));

    rig.ctl.Settings().fileName = L"clip.wav";          /* audior: --output clip.wav with mp3 gives clip.mp3 */
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\clip.mp3"));

    rig.ctl.Settings().format = Format::Wav;
    rig.ctl.Settings().fileName = L"Clip.MP3";
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\Clip.wav"));

    rig.ctl.Settings().fileName = L"take.2";            /* only what follows the last dot is an extension */
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\take.wav"));
}

TEST(Controller_ATypedNameIsNeverOverwritten)
{
    Rig rig;
    rig.ctl.Settings().fileName = L"meeting.mp3";
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\meeting.mp3"));
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\meeting (2).mp3"));
    CHECK_EQ(rig.Record(), std::wstring(L"C:\\Rec\\meeting (3).mp3"));
}

TEST(Controller_SeparateTracksAvoidEitherExistingFile)
{
    Rig rig;
    rig.ctl.Settings().includeSystemAudio = true;
    rig.ctl.Settings().mixMode = MixMode::Separate;
    rig.ctl.Settings().fileName = L"call";
    rig.existing.insert(L"C:\\Rec\\call_system.mp3");        /* only the second of the pair is taken */

    CHECK(rig.ctl.Start());
    rig.ctl.Stop();
    const auto &files = rig.ctl.LastOutcome().files;
    CHECK_EQ(files.size(), std::size_t(2));
    CHECK_EQ(files[0], std::wstring(L"C:\\Rec\\call (2)_mic.mp3"));
    CHECK_EQ(files[1], std::wstring(L"C:\\Rec\\call (2)_system.mp3"));
}

TEST(Controller_ABadFileNameStopsTheStartWithAReason)
{
    Rig rig;
    rig.ctl.Settings().fileName = L"a:b";
    CHECK(!rig.ctl.Start());
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::Failed);
    CHECK(!rig.ctl.LastOutcome().message.empty());

    rig.ctl.Settings().fileName = L"nul";
    CHECK(!rig.ctl.Start());

    rig.ctl.Settings().fileName = L"fine";
    CHECK(rig.ctl.Start());
}

TEST(Controller_AnOverlongPathIsRefused)
{
    Rig rig;
    rig.ctl.Settings().outputDirectory = L"C:\\" + std::wstring(240, L'd');
    rig.ctl.Settings().fileName = std::wstring(30, L'n');       /* 3 + 240 + 1 + 30 + 4 = 278 */
    CHECK(!rig.ctl.Start());
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::Failed);
    CHECK(rig.ctl.LastOutcome().message.find(L"too long") != std::wstring::npos);

    rig.ctl.Settings().fileName = std::wstring(8, L'n');        /* 3 + 240 + 1 + 8 + 4 = 256: fits */
    CHECK(rig.ctl.Start());
}

TEST(Controller_TheLongestOfTheTwoTracksCountsTowardsTheLimit)
{
    Rig rig;
    rig.ctl.Settings().includeSystemAudio = true;
    rig.ctl.Settings().mixMode = MixMode::Separate;
    rig.ctl.Settings().outputDirectory = L"C:\\" + std::wstring(240, L'd');
    rig.ctl.Settings().fileName = std::wstring(6, L'n');        /* 254; "_mic" makes 258, "_system" 261 */
    CHECK(!rig.ctl.Start());                                    /* only the longer of the two is over the limit */
    rig.ctl.Settings().mixMode = MixMode::Merge;
    CHECK(rig.ctl.Start());                                     /* one file at 254 fits */
}

TEST(Controller_AFailedStartDoesNotUseUpAName)
{
    Rig rig;
    rig.ctl.Settings().mergeMicPercent = 500;           /* invalid */
    CHECK(!rig.ctl.Start());
    rig.ctl.Settings().mergeMicPercent = 65;
    CHECK(rig.ctl.Start());
    rig.ctl.Stop();
    CHECK_EQ(rig.ctl.LastOutcome().files[0], std::wstring(L"C:\\Rec\\Audior-GUI-20261010-153045.mp3"));
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

TEST(Controller_SystemAudioAloneRunsOnlyTheSystemLane)
{
    Rig rig;
    rig.ctl.Settings().includeMicrophone = false;
    rig.ctl.Settings().includeSystemAudio = true;
    CHECK(!rig.ctl.StreamEnabled(StreamId::Microphone));
    CHECK(rig.ctl.StreamEnabled(StreamId::System));

    CHECK(rig.ctl.Start());
    rig.Run(1.0);
    CHECK(!rig.ctl.Lane(StreamId::Microphone).HasAudio());
    CHECK(rig.ctl.Lane(StreamId::System).HasAudio());
    rig.ctl.Stop();
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::Saved);
}

TEST(Controller_StartNeedsAStream)
{
    Rig rig;
    rig.ctl.Settings().includeMicrophone = false;
    rig.ctl.Settings().includeSystemAudio = false;
    CHECK(!rig.ctl.Start());
    CHECK(rig.ctl.GetPhase() == Phase::Idle);
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::Failed);
    CHECK(!rig.ctl.LastOutcome().message.empty());
}

/* ---- live transcripts ---------------------------------------------- */

namespace {

/* Counts how often each stream's transcript is asked for. */
class TextSpy : public SimulatedEngine {
public:
    using SimulatedEngine::SimulatedEngine;
    int pulls[kStreamCount] = {0, 0};
    std::size_t TakeTranscript(StreamId stream, TranscriptEvent *out, std::size_t capacity) override
    {
        ++pulls[StreamIndex(stream)];
        return SimulatedEngine::TakeTranscript(stream, out, capacity);
    }
};

}  // namespace

TEST(Controller_ATranscriptFlowsIntoItsLane)
{
    Rig rig;
    rig.ctl.Settings().transcribeMicrophone = true;
    CHECK(rig.ctl.Start());
    rig.Run(4.5);

    const TranscriptLane &mic = rig.ctl.Transcript(StreamId::Microphone);
    CHECK_EQ(mic.Count(), std::size_t(1));
    CHECK(mic.At(0).isFinal);
    CHECK(std::wstring(mic.At(0).text).compare(0, 4, L"Okay") == 0);
    CHECK_EQ(rig.ctl.Transcript(StreamId::System).Count(), std::size_t(0));
    CHECK(rig.ctl.TranscriptionStatus(StreamId::Microphone).state == TranscriptState::Live);
    CHECK(rig.ctl.TranscriptionStatus(StreamId::System).state == TranscriptState::Off);
}

TEST(Controller_ShowsATranscriptOnlyForAStreamSwitchedOnAndRecorded)
{
    Rig rig;
    CHECK(rig.ctl.CanTranscribe());
    CHECK(!rig.ctl.ShowsTranscript(StreamId::Microphone));          /* off until asked for */
    rig.ctl.Settings().transcribeMicrophone = true;
    CHECK(rig.ctl.ShowsTranscript(StreamId::Microphone));

    rig.ctl.Settings().transcribeSystem = true;                     /* system audio is not recorded */
    CHECK(!rig.ctl.ShowsTranscript(StreamId::System));
    rig.ctl.Settings().includeSystemAudio = true;
    CHECK(rig.ctl.ShowsTranscript(StreamId::System));
}

TEST(Controller_OffersTheModelsTheEngineHas)
{
    Rig rig;
    const std::vector<TranscriptModel> &models = rig.ctl.TranscriptModels();
    CHECK(!models.empty());
    CHECK(rig.ctl.CanTranscribe());
    CHECK_EQ(models.size(), rig.engine.TranscriptModels().size());
    CHECK_EQ(models[0].file, rig.engine.TranscriptModels()[0].file);
    CHECK(rig.ctl.CanTranscribeOnGpu());
}

TEST(Controller_AnEngineThatCannotTranscribeShowsNoTranscript)
{
    RefusingEngine engine;
    RecorderController ctl(engine, [] { return LocalTime(); });
    ctl.Settings().transcribeMicrophone = true;
    CHECK(!ctl.CanTranscribe());
    CHECK(ctl.TranscriptModels().empty());                          /* nothing to choose from */
    CHECK(!ctl.CanTranscribeOnGpu());
    CHECK(!ctl.ShowsTranscript(StreamId::Microphone));
    CHECK(ctl.TranscriptionStatus(StreamId::Microphone).state == TranscriptState::Off);
    CHECK_EQ(ctl.Transcript(StreamId::Microphone).Count(), std::size_t(0));
}

TEST(Controller_TheTranscriptSettingsAreFixedWhileRecording)
{
    Rig rig;
    rig.ctl.Start();
    rig.ctl.Settings().transcribeMicrophone = true;                 /* changed behind the recording's back */
    CHECK(!rig.ctl.ShowsTranscript(StreamId::Microphone));
    rig.Run(5.0);
    CHECK_EQ(rig.ctl.Transcript(StreamId::Microphone).Count(), std::size_t(0));

    rig.ctl.Stop();
    CHECK(rig.ctl.ShowsTranscript(StreamId::Microphone));           /* the draft applies again */
}

TEST(Controller_TheTranscriptStaysAfterStopUntilTheNextStart)
{
    Rig rig;
    rig.ctl.Settings().transcribeMicrophone = true;
    rig.ctl.Start();
    rig.Run(4.5);
    rig.ctl.Stop();
    CHECK_EQ(rig.ctl.Transcript(StreamId::Microphone).Count(), std::size_t(1));
    CHECK(rig.ctl.TranscriptionStatus(StreamId::Microphone).state == TranscriptState::Off);

    rig.ctl.Start();
    CHECK_EQ(rig.ctl.Transcript(StreamId::Microphone).Count(), std::size_t(0));
}

TEST(Controller_StoppingMakesTheLineBeingSpokenFinal)
{
    Rig rig;
    rig.ctl.Settings().transcribeMicrophone = true;
    rig.ctl.Start();
    rig.Run(2.0);
    CHECK_EQ(rig.ctl.Transcript(StreamId::Microphone).Count(), std::size_t(1));
    CHECK(!rig.ctl.Transcript(StreamId::Microphone).At(0).isFinal);
    rig.ctl.Stop();
    CHECK(rig.ctl.Transcript(StreamId::Microphone).At(0).isFinal);
}

TEST(Controller_ATranscriptionFailureDoesNotTouchTheRecording)
{
    Rig rig;
    rig.ctl.Settings().transcribeMicrophone = true;
    rig.engine.FailTranscriptAfter(2.0, L"The speech engine stopped responding.");
    rig.ctl.Start();
    rig.Run(5.0);

    CHECK(rig.ctl.GetPhase() == Phase::Recording);                  /* the recording carries on */
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::None);
    CHECK(rig.ctl.TranscriptionStatus(StreamId::Microphone).state == TranscriptState::Failed);
    CHECK_EQ(rig.ctl.TranscriptionProblem(StreamId::Microphone),
             std::wstring(L"The speech engine stopped responding."));
    CHECK(rig.ctl.Transcript(StreamId::Microphone).Count() > 0);    /* what came before stays */

    rig.ctl.Stop();
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::Saved);
    CHECK(rig.ctl.TranscriptionStatus(StreamId::Microphone).state == TranscriptState::Failed);   /* still marked */

    rig.ctl.Start();                                                /* the next recording starts clean */
    CHECK(rig.ctl.TranscriptionStatus(StreamId::Microphone).state == TranscriptState::Off);
    CHECK(rig.ctl.TranscriptionProblem(StreamId::Microphone).empty());
}

TEST(Controller_TheTranscriptSlideSettlesSoTheTimerCanStop)
{
    Rig rig;
    rig.ctl.Settings().transcribeMicrophone = true;
    rig.ctl.Start();
    rig.Run(1.5);
    rig.ctl.Stop();
    rig.Run(10.0);
    CHECK(!rig.ctl.Animating());
}

TEST(Controller_AsksOnlyForTheTranscriptsSwitchedOn)
{
    double now = 0.0;
    TextSpy engine([&now] { return now; });
    RecorderController ctl(engine, [] { return LocalTime(); });
    ctl.Settings().outputDirectory = L"C:\\Rec";
    ctl.Settings().includeSystemAudio = true;

    ctl.Start();                                                    /* nothing is switched on */
    now = 2.0;
    ctl.Tick(2.0);
    ctl.Stop();
    CHECK_EQ(engine.pulls[0] + engine.pulls[1], 0);

    ctl.Settings().transcribeSystem = true;
    ctl.Start();
    now = 4.0;
    ctl.Tick(2.0);
    CHECK_EQ(engine.pulls[0], 0);
    CHECK(engine.pulls[1] > 0);
}

/* ---- the start-up set-up of transcription ---- */

namespace {

/* A set-up source the test speaks for. */
class FakeSetup : public ISetupSource {
public:
    bool Poll(SetupStatus *out) override
    {
        if (!pending)
            return false;
        *out = next;
        pending = false;
        return true;
    }
    void Answer(bool download) override { answers.push_back(download); }
    void Cancel() override { ++cancels; }
    void Retry() override { ++retries; }

    void Say(const SetupStatus &status)
    {
        next = status;
        pending = true;
    }

    SetupStatus       next;
    bool              pending = false;
    std::vector<bool> answers;
    int               cancels = 0, retries = 0;
};

/* An engine without a speech model until it is told to look again. */
class LackingEngine : public SimulatedEngine {
public:
    explicit LackingEngine(Clock now) : SimulatedEngine(std::move(now)) { LackModels(); }
    void RescanTranscription() override
    {
        ++rescans;
        SimulatedEngine::RescanTranscription();
    }
    int rescans = 0;
};

struct SetupRig {
    double          now = 0.0;
    LackingEngine   engine;
    RecorderController ctl;
    FakeSetup       setup;

    SetupRig() : engine([this] { return now; }), ctl(engine, [] { return LocalTime(); }) { ctl.AttachSetup(&setup); }

    void Run(double seconds)
    {
        for (double t = 0.0; t < seconds - 1e-9; t += 0.25)
            ctl.Tick(0.25);
    }
};

SetupStatus Status(SetupPhase phase, SetupResult result = SetupResult::None)
{
    SetupStatus s;
    s.phase = phase;
    s.result = result;
    s.title = L"title";
    return s;
}

}  // namespace

TEST(Setup_ControllerWithoutASetupHasNothingToShow)
{
    Rig rig;
    CHECK(!rig.ctl.HasSetup());
    CHECK(!rig.ctl.SetupVisible());
    CHECK(!rig.ctl.SetupBusy());
    rig.ctl.AnswerSetup(true);                                      /* harmless */
    rig.ctl.CancelSetup();
    rig.ctl.RetrySetup();
    rig.Run(1.0);
    CHECK(!rig.ctl.SetupVisible());
}

TEST(Setup_ControllerShowsWhatTheSetupSays)
{
    SetupRig rig;
    CHECK(rig.ctl.HasSetup());
    CHECK(!rig.ctl.SetupVisible());                                 /* nothing said yet */
    const std::uint32_t before = rig.ctl.SetupVersion();

    rig.setup.Say(Status(SetupPhase::Scanning));
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.SetupVisible());
    CHECK(rig.ctl.Setup().phase == SetupPhase::Scanning);
    CHECK(rig.ctl.SetupVersion() != before);

    const std::uint32_t during = rig.ctl.SetupVersion();
    rig.ctl.Tick(0.016);                                            /* nothing new: nothing changes */
    CHECK_EQ(rig.ctl.SetupVersion(), during);
}

TEST(Setup_ControllerKeepsTickingOnlyWhileTheSetupHasWorkToDo)
{
    SetupRig rig;
    CHECK(!rig.ctl.SetupBusy());                                    /* nothing said: idle */
    rig.setup.Say(Status(SetupPhase::Scanning));
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.SetupBusy());

    rig.setup.Say(Status(SetupPhase::Asking));
    rig.ctl.Tick(0.016);
    CHECK(!rig.ctl.SetupBusy());                                    /* waiting for the user costs nothing */

    rig.ctl.AnswerSetup(true);
    CHECK(rig.ctl.SetupBusy());                                     /* but the answer to the answer is awaited ... */
    rig.Run(3.0);
    CHECK(!rig.ctl.SetupBusy());                                    /* ... not for ever */

    rig.setup.Say(Status(SetupPhase::Downloading));
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.SetupBusy());
}

TEST(Setup_ControllerPassesTheUsersAnswersOn)
{
    SetupRig rig;
    rig.ctl.AnswerSetup(true);
    rig.ctl.AnswerSetup(false);
    rig.ctl.CancelSetup();
    rig.ctl.RetrySetup();
    CHECK_EQ(rig.setup.answers.size(), std::size_t(2));
    CHECK(rig.setup.answers[0] && !rig.setup.answers[1]);
    CHECK_EQ(rig.setup.cancels, 1);
    CHECK_EQ(rig.setup.retries, 1);
}

TEST(Setup_NoUsableCardTurnsTheGpuSettingOffOncePerRun)
{
    SetupRig rig;
    CHECK(rig.ctl.Settings().transcribeGpu);
    SetupStatus asking = Status(SetupPhase::Asking);
    asking.scanned = true;
    asking.gpuUsable = false;
    rig.setup.Say(asking);
    rig.ctl.Tick(0.016);
    CHECK(!rig.ctl.Settings().transcribeGpu);

    rig.ctl.Settings().transcribeGpu = true;                        /* the user insists */
    SetupStatus downloading = asking;
    downloading.phase = SetupPhase::Downloading;
    rig.setup.Say(downloading);
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.Settings().transcribeGpu);                        /* and is not overruled by the next message of the same run */

    rig.setup.Say(Status(SetupPhase::Scanning));                    /* a new run (Retry) scans afresh */
    rig.ctl.Tick(0.016);
    rig.setup.Say(asking);
    rig.ctl.Tick(0.016);
    CHECK(!rig.ctl.Settings().transcribeGpu);
}

TEST(Setup_AUsableCardOrNoScanYetLeavesTheGpuSettingAlone)
{
    SetupRig rig;
    rig.setup.Say(Status(SetupPhase::Scanning));                    /* not scanned yet: gpuUsable means nothing */
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.Settings().transcribeGpu);

    SetupStatus found = Status(SetupPhase::Finished, SetupResult::Ready);
    found.scanned = true;
    found.gpuUsable = true;
    rig.setup.Say(found);
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.Settings().transcribeGpu);

    SetupRig cpuOnly;                                               /* --transcribe-cpu is never turned back on */
    cpuOnly.ctl.Settings().transcribeGpu = false;
    cpuOnly.setup.Say(found);
    cpuOnly.ctl.Tick(0.016);
    CHECK(!cpuOnly.ctl.Settings().transcribeGpu);
}

TEST(Setup_NewModelsAreTakenUpOncePerRun)
{
    SetupRig rig;
    CHECK(!rig.ctl.CanTranscribe());
    CHECK(rig.ctl.TranscriptModels().empty());

    SetupStatus done = Status(SetupPhase::Finished, SetupResult::Ready);
    done.scanned = true;
    done.modelsChanged = true;
    rig.setup.Say(done);
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.CanTranscribe());                                 /* the Transcript controls can appear */
    CHECK(!rig.ctl.TranscriptModels().empty());
    CHECK_EQ(rig.engine.rescans, 1);

    rig.setup.Say(done);                                            /* the same message again does not scan again */
    rig.ctl.Tick(0.016);
    CHECK_EQ(rig.engine.rescans, 1);
}

TEST(Setup_ASuccessGoesAwayByItselfAndAFailureStays)
{
    SetupRig rig;
    rig.setup.Say(Status(SetupPhase::Finished, SetupResult::Ready));
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.SetupVisible());
    rig.Run(5.0);
    CHECK(rig.ctl.SetupVisible());                                  /* long enough to be read */
    const std::uint32_t before = rig.ctl.SetupVersion();
    rig.Run(6.0);
    CHECK(!rig.ctl.SetupVisible());
    CHECK(rig.ctl.SetupVersion() != before);                        /* the view is told */
    CHECK(!rig.ctl.SetupBusy());

    rig.setup.Say(Status(SetupPhase::Finished, SetupResult::Failed));
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.SetupVisible());
    CHECK(!rig.ctl.SetupBusy());                                    /* nothing to count down */
    rig.Run(60.0);
    CHECK(rig.ctl.SetupVisible());                                  /* until the user has read it */
    rig.ctl.DismissSetup();
    CHECK(!rig.ctl.SetupVisible());
}

TEST(Setup_RetryBringsTheNotificationBack)
{
    SetupRig rig;
    rig.setup.Say(Status(SetupPhase::Finished, SetupResult::Failed));
    rig.ctl.Tick(0.016);
    rig.ctl.DismissSetup();
    CHECK(!rig.ctl.SetupVisible());

    rig.ctl.RetrySetup();
    CHECK(rig.ctl.SetupVisible());                                  /* until the set-up has its new say */
    CHECK(rig.ctl.SetupBusy());
    rig.setup.Say(Status(SetupPhase::Scanning));
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.SetupVisible() && rig.ctl.Setup().phase == SetupPhase::Scanning);
}

TEST(Setup_AQuietSetupShowsNothingButKeepsWhatItFoundOut)
{
    SetupRig rig;
    SetupStatus quiet;                                              /* Idle, but scanned: a "no" remembered from before */
    quiet.scanned = true;
    quiet.canRetry = true;
    rig.setup.Say(quiet);
    rig.ctl.Tick(0.016);
    CHECK(!rig.ctl.SetupVisible());
    CHECK(rig.ctl.Setup().canRetry);                                /* the Transcript section offers the download */
    CHECK(!rig.ctl.SetupBusy());
}
