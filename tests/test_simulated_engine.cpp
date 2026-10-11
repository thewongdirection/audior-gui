#include <algorithm>
#include <cstdint>
#include <vector>

#include "../src/core/asr_models.h"
#include "../src/core/lag_governor.h"
#include "../src/core/asr_languages.h"
#include "../src/engine/simulated_engine.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

struct Rig {
    double now = 0.0;
    SimulatedEngine engine;
    RecordingSettings settings;

    explicit Rig(std::uint64_t seed = 1) : engine([this] { return now; }, seed)
    {
        settings.outputDirectory = L"C:\\Rec";
    }

    bool Start(const wchar_t *path = L"C:\\Rec\\a.mp3")
    {
        std::wstring error;
        return engine.Start(settings, path, &error);
    }

    /* Up to `limit` bins the stream has produced since the last call. */
    std::vector<PeakBin> Take(StreamId stream, std::size_t limit = SIZE_MAX)
    {
        std::vector<PeakBin> all;
        PeakBin chunk[256];
        while (all.size() < limit) {
            const std::size_t n = engine.TakePeaks(
                stream, chunk, std::min<std::size_t>(256, limit - all.size()));
            if (n == 0)
                break;
            all.insert(all.end(), chunk, chunk + n);
        }
        return all;
    }

    /* Up to `limit` transcript events the stream has produced since the last call. */
    std::vector<TranscriptEvent> TakeText(StreamId stream, std::size_t limit = SIZE_MAX)
    {
        std::vector<TranscriptEvent> all;
        TranscriptEvent chunk[8];
        while (all.size() < limit) {
            const std::size_t n = engine.TakeTranscript(
                stream, chunk, std::min<std::size_t>(8, limit - all.size()));
            if (n == 0)
                break;
            all.insert(all.end(), chunk, chunk + n);
        }
        return all;
    }
};

int Words(const TranscriptEvent &e)
{
    int words = 1;
    for (const wchar_t *c = e.text; *c; ++c)
        words += *c == L' ';
    return words;
}

float MaxHigh(const std::vector<PeakBin> &bins)
{
    float m = 0.0f;
    for (const PeakBin &b : bins)
        m = std::max(m, b.high);
    return m;
}

}  // namespace

TEST(Sim_ListsDevicesWithOneDefaultEach)
{
    Rig rig;
    for (DeviceKind kind : {DeviceKind::Microphone, DeviceKind::Playback}) {
        const auto devices = rig.engine.ListDevices(kind);
        CHECK(devices.size() >= 2);
        CHECK_EQ(std::count_if(devices.begin(), devices.end(),
                               [](const DeviceInfo &d) { return d.isDefault; }), 1);
        for (const DeviceInfo &d : devices)
            CHECK(!d.id.empty() && !d.name.empty());
    }
}

TEST(Sim_IsMarkedSimulated)
{
    Rig rig;
    CHECK(rig.engine.IsSimulated());
}

TEST(Sim_StartAndStop)
{
    Rig rig;
    CHECK(!rig.engine.IsRecording());
    CHECK(rig.Start());
    CHECK(rig.engine.IsRecording());
    rig.engine.Stop();
    CHECK(!rig.engine.IsRecording());
    CHECK(rig.engine.Failure().empty());
}

TEST(Sim_StartWhileRecordingIsRefused)
{
    Rig rig;
    CHECK(rig.Start());
    std::wstring error;
    CHECK(!rig.engine.Start(rig.settings, L"C:\\Rec\\b.mp3", &error));
    CHECK(!error.empty());
    CHECK(rig.engine.IsRecording());
}

TEST(Sim_StopWhenIdleDoesNothing)
{
    Rig rig;
    rig.engine.Stop();
    CHECK(!rig.engine.IsRecording());
    CHECK_NEAR(rig.engine.Stats().seconds, 0.0, 0.0);
}

TEST(Sim_ProducesBinsAtTheRealTimeRate)
{
    Rig rig;
    rig.Start();
    CHECK_EQ(rig.Take(StreamId::Microphone).size(), std::size_t(0));

    rig.now = 1.0;
    CHECK_EQ(rig.Take(StreamId::Microphone).size(), std::size_t(100));
    CHECK_EQ(rig.Take(StreamId::Microphone).size(), std::size_t(0));    /* nothing new */

    rig.now = 1.5;
    CHECK_EQ(rig.Take(StreamId::Microphone).size(), std::size_t(50));
}

TEST(Sim_HonoursTheCapacityGiven)
{
    Rig rig;
    rig.Start();
    rig.now = 10.0;
    PeakBin buf[64];
    CHECK_EQ(rig.engine.TakePeaks(StreamId::Microphone, buf, 64), std::size_t(64));
    CHECK_EQ(rig.engine.TakePeaks(StreamId::Microphone, buf, 64), std::size_t(64));
    CHECK_EQ(rig.engine.TakePeaks(StreamId::Microphone, buf, 0), std::size_t(0));
    CHECK_EQ(rig.Take(StreamId::Microphone).size(), std::size_t(1000 - 128));
}

TEST(Sim_SystemStreamExistsOnlyWhenIncluded)
{
    Rig rig;
    rig.Start();
    rig.now = 1.0;
    CHECK_EQ(rig.Take(StreamId::System).size(), std::size_t(0));
    rig.engine.Stop();

    rig.settings.includeSystemAudio = true;
    rig.Start();
    rig.now = 2.0;
    CHECK_EQ(rig.Take(StreamId::System).size(), std::size_t(100));
}

TEST(Sim_BinsAreWellFormed)
{
    Rig rig;
    rig.settings.includeSystemAudio = true;
    rig.settings.micAmplifyPercent = 300;
    rig.Start();
    rig.now = 60.0;
    for (StreamId stream : {StreamId::Microphone, StreamId::System}) {
        for (const PeakBin &b : rig.Take(stream)) {
            CHECK(b.low <= 0.0f && b.low >= -1.0f);
            CHECK(b.high >= 0.0f && b.high <= 1.0f);
        }
    }
}

TEST(Sim_SameSeedSameAudioDifferentSeedDifferentAudio)
{
    Rig a(7), b(7), c(8);
    for (Rig *r : {&a, &b, &c}) {
        r->Start();
        r->now = 5.0;
    }
    const auto ba = a.Take(StreamId::Microphone);
    const auto bb = b.Take(StreamId::Microphone);
    const auto bc = c.Take(StreamId::Microphone);
    CHECK_EQ(ba.size(), bb.size());

    bool same = true, differs = false;
    for (std::size_t i = 0; i < ba.size(); ++i) {
        same = same && ba[i].high == bb[i].high && ba[i].low == bb[i].low;
        differs = differs || ba[i].high != bc[i].high;
    }
    CHECK(same);
    CHECK(differs);
}

TEST(Sim_AudioDoesNotDependOnHowOftenItIsPulled)
{
    Rig once, often;
    once.Start();
    often.Start();

    std::vector<PeakBin> piecewise;
    for (int i = 1; i <= 50; ++i) {
        often.now = i * 0.1;
        const auto part = often.Take(StreamId::Microphone);
        piecewise.insert(piecewise.end(), part.begin(), part.end());
    }
    once.now = 5.0;
    const auto whole = once.Take(StreamId::Microphone);

    CHECK_EQ(whole.size(), piecewise.size());
    for (std::size_t i = 0; i < whole.size() && i < piecewise.size(); ++i)
        CHECK(whole[i].high == piecewise[i].high && whole[i].low == piecewise[i].low);
}

TEST(Sim_MicGainScalesTheSignal)
{
    Rig quiet, loud;
    loud.settings.micAmplifyPercent = 100;              /* 2x */
    quiet.Start();
    loud.Start();
    quiet.now = loud.now = 20.0;
    const auto q = quiet.Take(StreamId::Microphone);
    const auto l = loud.Take(StreamId::Microphone);

    CHECK_EQ(q.size(), l.size());
    for (std::size_t i = 0; i < q.size() && i < l.size(); ++i) {
        CHECK_NEAR(l[i].high, 2.0f * q[i].high, 1e-5);
        CHECK_NEAR(l[i].low, 2.0f * q[i].low, 1e-5);
    }
}

TEST(Sim_HighGainClipsAtFullScale)
{
    Rig rig;
    rig.settings.micAmplifyPercent = 300;
    rig.Start();
    rig.now = 120.0;
    CHECK_NEAR(MaxHigh(rig.Take(StreamId::Microphone)), 1.0, 1e-6);

    Rig normal;                                         /* at unity gain it does not */
    normal.Start();
    normal.now = 120.0;
    CHECK(MaxHigh(normal.Take(StreamId::Microphone)) < 0.9f);
}

TEST(Sim_FilesFollowTheMixMode)
{
    Rig rig;
    rig.Start(L"C:\\Rec\\a.mp3");
    CHECK_EQ(rig.engine.OutputFiles().size(), std::size_t(1));
    rig.engine.Stop();

    rig.settings.includeSystemAudio = true;
    rig.settings.mixMode = MixMode::Separate;
    rig.Start(L"C:\\Rec\\a.mp3");
    const auto files = rig.engine.OutputFiles();
    CHECK_EQ(files.size(), std::size_t(2));
    CHECK_EQ(files[0], std::wstring(L"C:\\Rec\\a_mic.mp3"));
    CHECK_EQ(files[1], std::wstring(L"C:\\Rec\\a_system.mp3"));
}

TEST(Sim_StatsReportTimeAndSize)
{
    Rig rig;
    rig.Start();
    rig.now = 10.0;
    RecordingStats st = rig.engine.Stats();
    CHECK_NEAR(st.seconds, 10.0, 1e-9);
    CHECK_EQ(st.bytesWritten, std::uint64_t(160000));           /* 128 kbit/s x 10 s */
    rig.engine.Stop();

    rig.settings.format = Format::Wav;
    rig.settings.includeSystemAudio = true;
    rig.settings.mixMode = MixMode::Separate;
    rig.Start(L"C:\\Rec\\a.wav");
    rig.now = 20.0;
    CHECK_EQ(rig.engine.Stats().bytesWritten, std::uint64_t(2 * 1920000));  /* two 48k/16/2 WAVs */
}

TEST(Sim_StatsFreezeWhenStopped)
{
    Rig rig;
    rig.Start();
    rig.now = 3.0;
    rig.engine.Stop();
    rig.now = 10.0;
    CHECK_NEAR(rig.engine.Stats().seconds, 3.0, 1e-9);
    CHECK_EQ(rig.Take(StreamId::Microphone).size(), std::size_t(300));
}

TEST(Sim_FailureEndsTheRecordingByItself)
{
    Rig rig;
    rig.engine.FailAfter(2.0, L"Audio capture failed (hr=0x88890004)");
    rig.Start();

    rig.now = 1.5;
    CHECK(rig.engine.IsRecording());
    rig.now = 5.0;
    CHECK(!rig.engine.IsRecording());
    CHECK_EQ(rig.engine.Failure(), std::wstring(L"Audio capture failed (hr=0x88890004)"));
    CHECK_NEAR(rig.engine.Stats().seconds, 2.0, 1e-9);          /* cut at the failure */
    CHECK_EQ(rig.Take(StreamId::Microphone).size(), std::size_t(200));
}

TEST(Sim_AFailureAlsoShowsWhenAskingForStatsFirst)
{
    Rig rig;
    rig.engine.FailAfter(1.0, L"boom");
    rig.Start();
    rig.now = 4.0;
    CHECK_NEAR(rig.engine.Stats().seconds, 1.0, 1e-9);          /* no IsRecording() call before */
    CHECK_EQ(rig.engine.Failure(), std::wstring(L"boom"));
}

TEST(Sim_AFailureAppliesToOneRecordingOnly)
{
    Rig rig;
    rig.engine.FailAfter(1.0, L"boom");
    rig.Start();
    rig.now = 2.0;
    CHECK(!rig.engine.IsRecording());
    CHECK_EQ(rig.engine.Failure(), std::wstring(L"boom"));

    rig.Start();                                        /* the next one is healthy */
    rig.now = 10.0;
    CHECK(rig.engine.IsRecording());
    CHECK(rig.engine.Failure().empty());
}

TEST(Sim_StartNoticesAFailureNobodyHasPolledYet)
{
    Rig rig;
    rig.engine.FailAfter(1.0, L"boom");
    rig.Start();
    rig.now = 5.0;                                      /* it failed at t=1; nothing looked */
    CHECK(rig.Start());                                 /* so Start() must not say "already running" */
}

/* ---- the live transcript ------------------------------------------- */

TEST(Sim_CanTranscribe)
{
    Rig rig;
    CHECK(rig.engine.CanTranscribe());
}

TEST(Sim_CanFallBehindOnRequest)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.engine.FallBehindAfter(5.0);
    rig.Start();
    rig.now = 3.0;
    CHECK(rig.engine.Transcription(StreamId::Microphone).state == TranscriptState::Live);
    CHECK(!rig.engine.Transcription(StreamId::Microphone).smallerModel);
    rig.now = 6.0;
    const TranscriptStatus behind = rig.engine.Transcription(StreamId::Microphone);
    CHECK(behind.state == TranscriptState::CatchingUp);
    CHECK(behind.lagSeconds > LagGovernor::kCatchUpLag);
    CHECK(behind.smallerModel);

    rig.engine.Stop();                                  /* it applies to the one recording only */
    rig.Start();
    rig.now = 12.0;
    CHECK(rig.engine.Transcription(StreamId::Microphone).state == TranscriptState::Live);
}

TEST(Sim_OffersTheWholeTableOfModels)
{
    Rig rig;
    const std::vector<TranscriptModel> models = rig.engine.TranscriptModels();
    CHECK_EQ(models.size(), kAsrModelCount);
    for (std::size_t i = 0; i < models.size() && i < kAsrModelCount; ++i)
        CHECK_EQ(models[i].file, std::wstring(kAsrModels[i].file));
}

TEST(Sim_TranscribesOnlyWhatIsSwitchedOn)
{
    Rig rig;
    rig.settings.includeSystemAudio = true;
    rig.Start();
    rig.now = 30.0;                                     /* nothing is switched on: no text, status Off */
    CHECK(rig.TakeText(StreamId::Microphone).empty());
    CHECK(rig.engine.Transcription(StreamId::Microphone).state == TranscriptState::Off);
    rig.engine.Stop();

    rig.settings.transcribeMicrophone = true;
    rig.settings.transcribeSystem = true;
    rig.Start();
    rig.now = 60.0;                                     /* 30 s into the second recording */
    CHECK(!rig.TakeText(StreamId::Microphone).empty());
    CHECK(!rig.TakeText(StreamId::System).empty());
}

TEST(Sim_NoTextForAStreamThatIsNotRecorded)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.settings.transcribeSystem = true;               /* but system audio is not included */
    rig.Start();
    rig.now = 30.0;
    CHECK(!rig.TakeText(StreamId::Microphone).empty());
    CHECK(rig.TakeText(StreamId::System).empty());
    CHECK(rig.engine.Transcription(StreamId::System).state == TranscriptState::Off);
}

TEST(Sim_TranscriptionStartsThenGoesLiveThenStops)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.Start();
    rig.now = 0.5;
    CHECK(rig.engine.Transcription(StreamId::Microphone).state == TranscriptState::Starting);
    rig.now = 2.0;
    const TranscriptStatus live = rig.engine.Transcription(StreamId::Microphone);
    CHECK(live.state == TranscriptState::Live);
    CHECK(live.lagSeconds > 0.0f);

    rig.engine.Stop();
    CHECK(rig.engine.Transcription(StreamId::Microphone).state == TranscriptState::Off);
}

TEST(Sim_AnUtteranceIsSpokenWordByWordThenFinal)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.Start();
    rig.now = 4.5;                                      /* the first utterance is over, the next has not begun */
    const auto events = rig.TakeText(StreamId::Microphone);

    CHECK_EQ(events.size(), std::size_t(12));           /* 11 words, then the final */
    for (std::size_t i = 0; i < events.size(); ++i) {
        CHECK_EQ(events[i].utterance, std::uint32_t(0));
        CHECK_NEAR(events[i].startSeconds, 0.8, 1e-9);
        CHECK_EQ(events[i].isFinal, i + 1 == events.size());
        if (i + 1 < events.size())
            CHECK_EQ(Words(events[i]), static_cast<int>(i) + 1);       /* one more word each time */
    }
    CHECK_EQ(std::wstring(events[10].text), std::wstring(events[11].text));    /* the final repeats the last partial */
    CHECK(std::wstring(events[11].text).compare(0, 4, L"Okay") == 0);
}

TEST(Sim_ChineseIsHeardTwoCharactersAtATimeWithNoSpaces)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    CHECK(FindSpeechLanguage(L"zh", &rig.settings.transcribeLanguage));
    rig.Start();
    rig.now = 4.5;
    const auto events = rig.TakeText(StreamId::Microphone);

    CHECK_EQ(events.size(), std::size_t(12));           /* 21 characters: ten times two and one, then the final */
    CHECK(events.back().isFinal);
    const std::wstring all = events.back().text;
    CHECK_EQ(all, std::wstring(L"好的，我们开始吧。感谢大家这么快就赶过来。"));
    for (std::size_t i = 0; i + 1 < events.size(); ++i) {
        const std::wstring part = events[i].text;
        CHECK_EQ(part.size(), std::min<std::size_t>(2 * (i + 1), all.size()));
        CHECK(all.compare(0, part.size(), part) == 0);                  /* each is the start of the next */
    }
    rig.engine.Stop();
    Rig english;
    english.settings.transcribeMicrophone = true;
    english.Start();
    english.now = 4.5;
    CHECK(std::wstring(english.TakeText(StreamId::Microphone).back().text).compare(0, 4, L"Okay") == 0);   /* the other language still plays */
}

TEST(Sim_TextAppearsAtTheScriptedTimes)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.Start();
    rig.now = 0.7;
    CHECK(rig.TakeText(StreamId::Microphone).empty());  /* the first word comes at 0.8 s */
    rig.now = 0.9;
    CHECK_EQ(rig.TakeText(StreamId::Microphone).size(), std::size_t(1));
    rig.now = 1.1;
    CHECK_EQ(rig.TakeText(StreamId::Microphone).size(), std::size_t(1));
}

TEST(Sim_TranscriptDoesNotDependOnHowOftenItIsPulled)
{
    Rig once, often;
    for (Rig *r : {&once, &often}) {
        r->settings.transcribeMicrophone = true;
        r->Start();
    }
    std::vector<TranscriptEvent> piecewise;
    for (int i = 1; i <= 120; ++i) {
        often.now = i * 0.1;
        const auto part = often.TakeText(StreamId::Microphone);
        piecewise.insert(piecewise.end(), part.begin(), part.end());
    }
    once.now = 12.0;
    const auto whole = once.TakeText(StreamId::Microphone);

    CHECK_EQ(whole.size(), piecewise.size());
    for (std::size_t i = 0; i < whole.size() && i < piecewise.size(); ++i) {
        CHECK_EQ(whole[i].utterance, piecewise[i].utterance);
        CHECK_EQ(whole[i].isFinal, piecewise[i].isFinal);
        CHECK_EQ(std::wstring(whole[i].text), std::wstring(piecewise[i].text));
    }
}

TEST(Sim_TranscriptHonoursTheCapacityGiven)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.Start();
    rig.now = 20.0;
    TranscriptEvent buf[8];
    CHECK_EQ(rig.engine.TakeTranscript(StreamId::Microphone, buf, 5), std::size_t(5));
    CHECK_EQ(rig.engine.TakeTranscript(StreamId::Microphone, buf, 0), std::size_t(0));
    CHECK_EQ(buf[0].utterance, std::uint32_t(0));
    CHECK_EQ(Words(buf[0]), 1);
    CHECK_EQ(rig.engine.TakeTranscript(StreamId::Microphone, buf, 1), std::size_t(1));
    CHECK_EQ(Words(buf[0]), 6);                         /* it carried on where it stopped */
}

TEST(Sim_AWordIsHeardWrongThenCorrected)
{
    Rig rig;
    rig.settings.includeSystemAudio = true;
    rig.settings.transcribeSystem = true;
    rig.Start();
    rig.now = 30.0;

    bool heardWrong = false, corrected = false, wrongAfterCorrection = false;
    for (const TranscriptEvent &e : rig.TakeText(StreamId::System)) {
        if (e.utterance != 1)
            continue;
        const std::wstring text = e.text;
        const bool wrong = text.find(L"sign in") != std::wstring::npos;
        const bool right = text.find(L"signing") != std::wstring::npos;
        wrongAfterCorrection = wrongAfterCorrection || (corrected && wrong);
        heardWrong = heardWrong || wrong;
        corrected = corrected || right;
        if (e.isFinal)
            CHECK(right && !wrong);
    }
    CHECK(heardWrong);
    CHECK(corrected);
    CHECK(!wrongAfterCorrection);
}

TEST(Sim_TheConversationStartsOverAndTheIdsKeepCounting)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.Start();
    rig.now = 41.0;
    const auto first = rig.TakeText(StreamId::Microphone);
    CHECK(!first.empty());
    const std::uint32_t lastId = first.back().utterance;
    CHECK_EQ(lastId, std::uint32_t(4));                 /* five mic utterances in the script */

    rig.now = 44.0;
    const auto next = rig.TakeText(StreamId::Microphone);
    CHECK(!next.empty());
    CHECK_EQ(next.front().utterance, std::uint32_t(5));
    CHECK_NEAR(next.front().startSeconds, 42.8, 1e-9);
    CHECK(std::wstring(next.front().text).size() > 0);
}

TEST(Sim_EachRecordingStartsTheScriptOver)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.Start();
    rig.now = 6.0;
    const auto a = rig.TakeText(StreamId::Microphone);
    rig.engine.Stop();

    rig.now = 100.0;
    rig.Start();
    rig.now = 106.0;
    const auto b = rig.TakeText(StreamId::Microphone);
    CHECK_EQ(a.size(), b.size());
    CHECK(!b.empty());
    CHECK_EQ(b.front().utterance, std::uint32_t(0));
    CHECK_EQ(std::wstring(a.back().text), std::wstring(b.back().text));
}

TEST(Sim_StoppingFreezesTheTranscript)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.Start();
    rig.now = 3.0;
    rig.engine.Stop();
    rig.now = 30.0;
    const auto events = rig.TakeText(StreamId::Microphone);
    CHECK(!events.empty());
    CHECK(!events.back().isFinal);                      /* cut in the middle of the utterance */
    CHECK(events.back().startSeconds < 3.0);
}

TEST(Sim_ATranscriptionFailureLeavesTheRecordingRunning)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.engine.FailTranscriptAfter(2.0, L"The speech engine stopped responding.");
    rig.Start();
    rig.now = 1.0;
    CHECK(rig.engine.Transcription(StreamId::Microphone).state != TranscriptState::Failed);
    const std::size_t before = rig.TakeText(StreamId::Microphone).size();
    CHECK(before > 0);

    rig.now = 10.0;
    CHECK(rig.engine.IsRecording());                    /* the recording is untouched */
    CHECK(rig.engine.Failure().empty());
    CHECK(rig.engine.Transcription(StreamId::Microphone).state == TranscriptState::Failed);
    CHECK_EQ(rig.engine.TranscriptionProblem(StreamId::Microphone),
             std::wstring(L"The speech engine stopped responding."));
    CHECK_EQ(rig.Take(StreamId::Microphone).size(), std::size_t(1000));    /* the audio goes on */

    /* The text stopped where the failure cut it: what came before is still delivered, nothing after. */
    Rig healthy;
    healthy.settings.transcribeMicrophone = true;
    healthy.Start();
    healthy.now = 2.0;
    const std::size_t until = healthy.TakeText(StreamId::Microphone).size();
    CHECK_EQ(before + rig.TakeText(StreamId::Microphone).size(), until);
}

TEST(Sim_ATranscriptionFailureAppliesToOneRecordingOnly)
{
    Rig rig;
    rig.settings.transcribeMicrophone = true;
    rig.engine.FailTranscriptAfter(1.0, L"boom");
    rig.Start();
    rig.now = 2.0;
    CHECK(rig.engine.Transcription(StreamId::Microphone).state == TranscriptState::Failed);
    rig.engine.Stop();

    rig.Start();
    rig.now = 10.0;
    CHECK(rig.engine.Transcription(StreamId::Microphone).state == TranscriptState::Live);
}

TEST(Sim_MicrophoneStreamExistsOnlyWhenIncluded)
{
    Rig rig;
    rig.settings.includeMicrophone = false;
    rig.settings.includeSystemAudio = true;
    rig.Start();
    rig.now = 1.0;
    CHECK_EQ(rig.Take(StreamId::Microphone).size(), std::size_t(0));
    CHECK_EQ(rig.Take(StreamId::System).size(), std::size_t(100));
    CHECK_EQ(rig.engine.OutputFiles().size(), std::size_t(1));
}
