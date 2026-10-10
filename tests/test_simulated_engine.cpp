#include <algorithm>
#include <cstdint>
#include <vector>

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
};

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
