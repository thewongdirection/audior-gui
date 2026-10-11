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

TEST(Controller_ADeviceThatIsGoneFallsBackToTheSystemDefault)
{
    Rig rig;
    const std::wstring mic = rig.ctl.Devices(DeviceKind::Microphone).back().id;
    const std::wstring speakers = rig.ctl.Devices(DeviceKind::Playback).back().id;

    rig.ctl.Settings().microphoneId = mic;                                      /* both are plugged in: kept */
    rig.ctl.Settings().systemDeviceId = speakers;
    rig.ctl.DropUnavailableDevices();
    CHECK_EQ(rig.ctl.Settings().microphoneId, mic);
    CHECK_EQ(rig.ctl.Settings().systemDeviceId, speakers);

    rig.ctl.Settings().microphoneId = L"{0.0.1.00000000}.{unplugged}";
    rig.ctl.Settings().systemDeviceId = L"{0.0.0.00000000}.{unplugged}";
    rig.ctl.DropUnavailableDevices();
    CHECK(rig.ctl.Settings().microphoneId.empty());
    CHECK(rig.ctl.Settings().systemDeviceId.empty());
    CHECK(rig.ctl.Start());                                                     /* and recording works */
}

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

TEST(Controller_ATranscriptFileThatWouldBeTooLongIsRefusedBeforeRecording)
{
    Rig rig;
    rig.ctl.Settings().outputDirectory = L"C:\\" + std::wstring(240, L'd');
    rig.ctl.Settings().fileName = std::wstring(8, L'n');        /* the recording fits: 256 */
    rig.ctl.Settings().transcribeMicrophone = true;             /* but " - microphone.txt" makes 269 */
    CHECK(!rig.ctl.Start());
    CHECK(rig.ctl.LastOutcome().kind == Outcome::Kind::Failed);
    CHECK(rig.ctl.LastOutcome().message.find(L"transcript") != std::wstring::npos);     /* and says it is the transcript */
    CHECK(rig.ctl.LastOutcome().message.find(L"too long") != std::wstring::npos);

    rig.ctl.Settings().transcribeMicrophone = false;            /* without the transcript there is nothing too long */
    CHECK(rig.ctl.Start());
}

/* An engine without the helper or a model: the transcript switches are hidden, whatever the saved settings say. */
class DeafEngine : public SimulatedEngine {
public:
    using SimulatedEngine::SimulatedEngine;
    bool CanTranscribe() const override { return false; }
};

TEST(Controller_AnEngineThatCannotTranscribeLeavesTheTranscriptSwitchesOutOfTheNames)
{
    std::set<std::wstring> existing;
    LocalTime time;
    time.year = 2026; time.month = 10; time.day = 10;
    DeafEngine engine([] { return 0.0; });
    RecorderController ctl(engine, [&time] { return time; },
                           [&existing](const std::wstring &path) { return existing.count(path) > 0; });
    ctl.Settings().outputDirectory = L"C:\\" + std::wstring(240, L'd');
    ctl.Settings().fileName = std::wstring(8, L'n');            /* with a transcript name this would be 269 */
    ctl.Settings().transcribeMicrophone = true;
    CHECK(ctl.Start());                                         /* not refused for a transcript it cannot make */
    ctl.Stop();

    ctl.Settings().outputDirectory = L"C:\\Rec";
    ctl.Settings().fileName = L"a";
    existing.insert(L"C:\\Rec\\a - microphone.txt");            /* a leftover transcript does not take a number */
    CHECK(ctl.Start());
    ctl.Stop();
    CHECK_EQ(ctl.LastOutcome().files[0], std::wstring(L"C:\\Rec\\a.mp3"));
}

TEST(Controller_TheSystemTranscriptsLongerNameCanBeTheOnlyOneOverTheLimit)
{
    Rig rig;
    rig.ctl.Settings().includeSystemAudio = true;
    rig.ctl.Settings().outputDirectory = L"C:\\" + std::wstring(233, L'd');
    rig.ctl.Settings().fileName = std::wstring(5, L'n');        /* the recording is 246; " - microphone.txt" makes 259: just fits */
    rig.ctl.Settings().transcribeMicrophone = true;
    CHECK(rig.ctl.Start());
    rig.ctl.Stop();

    rig.ctl.Settings().transcribeSystem = true;                 /* " - system audio.txt" is 2 longer: 261 */
    CHECK(!rig.ctl.Start());
    CHECK(rig.ctl.LastOutcome().message.find(L"transcript") != std::wstring::npos);
}

TEST(Controller_ARecordingAndItsTranscriptsAreNumberedAlike)
{
    Rig rig;
    rig.ctl.Settings().fileName = L"meeting";
    rig.ctl.Settings().transcribeMicrophone = true;
    rig.existing.insert(L"C:\\Rec\\meeting - microphone.txt");      /* an earlier transcript is there; its audio (a WAV, say) is not */
    CHECK(rig.ctl.Start());
    rig.ctl.Stop();
    const Outcome &o = rig.ctl.LastOutcome();
    CHECK_EQ(o.files[0], std::wstring(L"C:\\Rec\\meeting (2).mp3"));                        /* the recording takes the next number... */
    CHECK_EQ(o.transcripts[0], std::wstring(L"C:\\Rec\\meeting (2) - microphone.txt"));      /* ...and its transcript has it too */

    Rig without;                                                /* the same files, but no transcript: the name is free */
    without.ctl.Settings().fileName = L"meeting";
    without.existing.insert(L"C:\\Rec\\meeting - microphone.txt");
    CHECK(without.ctl.Start());
    without.ctl.Stop();
    CHECK_EQ(without.ctl.LastOutcome().files[0], std::wstring(L"C:\\Rec\\meeting.mp3"));
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

TEST(Controller_TheOutcomeNamesTheTranscriptFilesOfTheTranscribedStreams)
{
    Rig rig;
    rig.ctl.Settings().includeSystemAudio = true;
    rig.ctl.Settings().transcribeMicrophone = rig.ctl.Settings().transcribeSystem = true;
    CHECK(rig.ctl.Start());
    rig.Run(1.0);
    rig.ctl.Stop();

    const Outcome &o = rig.ctl.LastOutcome();
    CHECK(o.kind == Outcome::Kind::Saved);
    CHECK_EQ(o.transcripts.size(), std::size_t(2));
    CHECK_EQ(o.transcripts[0], PlannedTranscriptFile(o.files[0], StreamId::Microphone));    /* beside the recording */
    CHECK_EQ(o.transcripts[1], PlannedTranscriptFile(o.files[0], StreamId::System));
    CHECK(o.transcriptProblem.empty());

    Rig onlyOne;                                                    /* a pane without a transcript has no file */
    onlyOne.ctl.Settings().includeSystemAudio = true;
    onlyOne.ctl.Settings().transcribeSystem = true;
    CHECK(onlyOne.ctl.Start());
    onlyOne.Run(1.0);
    onlyOne.ctl.Stop();
    CHECK_EQ(onlyOne.ctl.LastOutcome().transcripts.size(), std::size_t(1));
    CHECK_EQ(onlyOne.ctl.LastOutcome().transcripts[0], PlannedTranscriptFile(onlyOne.ctl.LastOutcome().files[0], StreamId::System));

    Rig none;                                                       /* no transcript asked for: none saved */
    none.Record();
    CHECK(none.ctl.LastOutcome().transcripts.empty());
}

namespace {

class UnsavedTranscript : public SimulatedEngine {
public:
    using SimulatedEngine::SimulatedEngine;
    std::wstring TranscriptFileProblem() override { return L"transcript not saved (Windows error 112)"; }
};

}  // namespace

TEST(Controller_ATranscriptThatCouldNotBeSavedIsReportedWithoutFailingTheRecording)
{
    double now = 0.0;
    LocalTime time;
    time.year = 2026; time.month = 10; time.day = 10; time.hour = 15; time.minute = 30; time.second = 45;
    UnsavedTranscript engine([&] { return now; });
    RecorderController ctl(engine, [&] { return time; }, [](const std::wstring &) { return false; });
    ctl.Settings().outputDirectory = L"C:\\Rec";
    ctl.Settings().transcribeMicrophone = true;
    CHECK(ctl.Start());
    ctl.Stop();

    CHECK(ctl.LastOutcome().kind == Outcome::Kind::Saved);          /* the audio is fine */
    CHECK_EQ(ctl.LastOutcome().transcriptProblem, std::wstring(L"transcript not saved (Windows error 112)"));
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

SetupStatus Status(SetupPhase phase, SetupResult result = SetupResult::None)
{
    SetupStatus s;
    s.phase = phase;
    s.result = result;
    s.title = L"title";
    return s;
}

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
    void Retry() override
    {
        ++retries;
        if (scansAtOnce)
            Say(Status(SetupPhase::Scanning));              /* as the real one does: it has something to say before Retry returns */
    }

    void Say(const SetupStatus &status)
    {
        next = status;
        pending = true;
    }

    SetupStatus       next;
    bool              pending = false;
    bool              scansAtOnce = true;
    std::vector<bool> answers;
    int               cancels = 0, retries = 0;
};

/* An engine without a speech model until it is told to look again. */
class LackingEngine : public SimulatedEngine {
public:
    explicit LackingEngine(Clock now) : SimulatedEngine(std::move(now)) { LackModels(); }
    bool Start(const RecordingSettings &settings, const std::wstring &path, std::wstring *error) override
    {
        started = settings;                             /* what the recording was really asked to do */
        return SimulatedEngine::Start(settings, path, error);
    }
    RecordingSettings started;
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

    SetupRig() : engine([this] { return now; }), ctl(engine, [] { return LocalTime(); })
    {
        ctl.Settings().outputDirectory = L"C:\\Rec";
        ctl.AttachSetup(&setup);
    }

    void Run(double seconds)
    {
        for (double t = 0.0; t < seconds - 1e-9; t += 0.25)
            ctl.Tick(0.25);
    }
};

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

TEST(Setup_NoUsableCardMeansNoCardForARecordingAndTheChoiceIsKept)
{
    SetupRig rig;
    CHECK(rig.ctl.Settings().transcribeGpu);
    CHECK(!rig.ctl.GpuUnusable());
    SetupStatus found = Status(SetupPhase::Asking);
    found.scanned = true;
    found.gpuUsable = false;
    rig.setup.Say(found);
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.GpuUnusable());
    CHECK(rig.ctl.Settings().transcribeGpu);                        /* the user's choice, which is saved, is not touched ... */

    CHECK(rig.ctl.Start());
    CHECK(!rig.engine.started.transcribeGpu);                       /* ... but the recording does not ask for a card that is not there */
    CHECK(!rig.ctl.Active().transcribeGpu);
    rig.ctl.Stop();
    CHECK(rig.ctl.Settings().transcribeGpu);
}

TEST(Setup_ALaterScanThatFindsACardGivesItBack)
{
    SetupRig rig;
    SetupStatus found = Status(SetupPhase::Finished, SetupResult::Ready);
    found.scanned = true;
    found.gpuUsable = false;
    rig.setup.Say(found);
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.GpuUnusable());

    rig.ctl.RetrySetup();                                           /* a driver was installed, say */
    found.gpuUsable = true;
    rig.setup.Say(found);
    rig.ctl.Tick(0.016);
    CHECK(!rig.ctl.GpuUnusable());
    CHECK(rig.ctl.Start());
    CHECK(rig.engine.started.transcribeGpu);                        /* the saved choice applies again */
    rig.ctl.Stop();
}

TEST(Setup_AUsableCardOrNoScanYetLeavesTheGpuChoiceAlone)
{
    SetupRig rig;
    SetupStatus early = Status(SetupPhase::Scanning);               /* not scanned yet: gpuUsable means nothing */
    early.gpuUsable = false;
    rig.setup.Say(early);
    rig.ctl.Tick(0.016);
    CHECK(!rig.ctl.GpuUnusable());

    SetupStatus found = Status(SetupPhase::Finished, SetupResult::Ready);
    found.scanned = true;
    found.gpuUsable = true;
    rig.setup.Say(found);
    rig.ctl.Tick(0.016);
    CHECK(!rig.ctl.GpuUnusable());
    CHECK(rig.ctl.Start());
    CHECK(rig.engine.started.transcribeGpu);
    rig.ctl.Stop();

    SetupRig cpuOnly;                                               /* --transcribe-cpu stays what it is */
    cpuOnly.ctl.Settings().transcribeGpu = false;
    cpuOnly.setup.Say(found);
    cpuOnly.ctl.Tick(0.016);
    CHECK(cpuOnly.ctl.Start());
    CHECK(!cpuOnly.engine.started.transcribeGpu);
    cpuOnly.ctl.Stop();
}

TEST(Setup_ANewRunLooksForModelsAfreshEvenIfItsFirstWordIsNeverSeen)
{
    SetupRig rig;
    rig.setup.scansAtOnce = false;                                  /* the controller never sees the new run say "Scanning" */
    SetupStatus found = Status(SetupPhase::Finished, SetupResult::Ready);
    found.scanned = true;
    found.modelsChanged = true;
    rig.setup.Say(found);
    rig.ctl.Tick(0.016);
    CHECK_EQ(rig.engine.rescans, 1);

    rig.ctl.RetrySetup();
    rig.setup.Say(found);                                           /* the set-up has run twice between two looks */
    rig.ctl.Tick(0.016);
    CHECK_EQ(rig.engine.rescans, 2);                                /* what the new run brought counts */
}

TEST(Setup_TheUsersNoIsKeptForTheSettingsFileAndFollowsTheSetup)
{
    SetupRig rig;
    CHECK(!rig.ctl.DownloadDeclined());
    rig.ctl.SetDownloadDeclined(true);                              /* what the settings file said at start-up */
    CHECK(rig.ctl.DownloadDeclined());

    SetupStatus said = Status(SetupPhase::Finished, SetupResult::Skipped);
    said.declined = true;
    rig.setup.Say(said);
    rig.ctl.Tick(0.016);
    CHECK(rig.ctl.DownloadDeclined());

    said.declined = false;                                          /* a yes to a later offer takes it back */
    rig.setup.Say(said);
    rig.ctl.Tick(0.016);
    CHECK(!rig.ctl.DownloadDeclined());
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
    CHECK(rig.ctl.SetupVisible());
    CHECK(rig.ctl.Setup().phase == SetupPhase::Scanning);           /* at once: not the old failure, with a live "Try again" */
    CHECK(rig.ctl.SetupBusy());
}

TEST(Setup_ARetryThatIsRefusedLeavesTheNotificationAlone)
{
    SetupRig rig;
    rig.setup.scansAtOnce = false;                                  /* (a run still in progress ignores Retry) */
    rig.setup.Say(Status(SetupPhase::Finished, SetupResult::Failed));
    rig.ctl.Tick(0.016);
    rig.ctl.DismissSetup();
    rig.ctl.RetrySetup();
    CHECK(!rig.ctl.SetupVisible());                                 /* nothing new was said: the dismissal stands */
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
