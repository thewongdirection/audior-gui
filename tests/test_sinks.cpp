/* Tests of the file sinks, the stop signal, the silence filler and the session clock. */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "../src/engine/hresult.h"
#include "../src/engine/mp3_sink.h"
#include "../src/engine/session_clock.h"
#include "../src/engine/silence_filler.h"
#include "../src/engine/stop_signal.h"
#include "../src/engine/wav_sink.h"
#include "temp_dir.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

std::uint32_t Le32(const std::vector<std::uint8_t> &b, std::size_t at)
{
    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + at, sizeof(v));
    return v;
}

std::uint16_t Le16(const std::vector<std::uint8_t> &b, std::size_t at)
{
    std::uint16_t v = 0;
    std::memcpy(&v, b.data() + at, sizeof(v));
    return v;
}

std::vector<std::int16_t> Tone(std::size_t frames)
{
    std::vector<std::int16_t> pcm;
    for (std::size_t i = 0; i < frames; ++i) {
        const std::int16_t s = static_cast<std::int16_t>(8000.0 * std::sin(0.05 * static_cast<double>(i)));
        pcm.push_back(s);
        pcm.push_back(s);
    }
    return pcm;
}

}  // namespace

/* ---- WAV ---- */

TEST(WavSink_WritesAValidFile)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"a.wav");
    HRESULT hr = E_FAIL;
    auto sink = WavSink::Create(path, &hr);
    CHECK(sink != nullptr);
    CHECK(SUCCEEDED(hr));

    const std::vector<std::int16_t> pcm = Tone(4800);
    CHECK(SUCCEEDED(sink->Write(pcm.data(), pcm.size() * sizeof(std::int16_t))));
    CHECK_EQ(sink->BytesWritten(), std::uint64_t(4800 * 4));
    CHECK(SUCCEEDED(sink->Close(true)));

    const std::vector<std::uint8_t> file = ReadFileBytes(path);
    CHECK_EQ(file.size(), std::size_t(44 + 4800 * 4));
    CHECK(std::memcmp(file.data(), "RIFF", 4) == 0);
    CHECK(std::memcmp(file.data() + 8, "WAVEfmt ", 8) == 0);
    CHECK(std::memcmp(file.data() + 36, "data", 4) == 0);
    CHECK_EQ(Le32(file, 4), std::uint32_t(36 + 4800 * 4));             /* RIFF size  */
    CHECK_EQ(Le16(file, 20), std::uint16_t(1));                        /* PCM        */
    CHECK_EQ(Le16(file, 22), std::uint16_t(2));                        /* stereo     */
    CHECK_EQ(Le32(file, 24), std::uint32_t(48000));
    CHECK_EQ(Le32(file, 28), std::uint32_t(192000));                   /* bytes/s    */
    CHECK_EQ(Le16(file, 32), std::uint16_t(4));                        /* frame size */
    CHECK_EQ(Le16(file, 34), std::uint16_t(16));
    CHECK_EQ(Le32(file, 40), std::uint32_t(4800 * 4));                 /* data size  */
    CHECK(std::memcmp(file.data() + 44, pcm.data(), pcm.size() * sizeof(std::int16_t)) == 0);
}

TEST(WavSink_AnEmptyRecordingIsStillAValidFile)
{
    TempDir dir;
    HRESULT hr = E_FAIL;
    auto sink = WavSink::Create(dir.Path(L"empty.wav"), &hr);
    CHECK(sink != nullptr);
    CHECK(SUCCEEDED(sink->Close(true)));
    const std::vector<std::uint8_t> file = ReadFileBytes(dir.Path(L"empty.wav"));
    CHECK_EQ(file.size(), std::size_t(44));
    CHECK_EQ(Le32(file, 40), std::uint32_t(0));
}

TEST(WavSink_NeverOverwritesAFile)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"keep.wav");
    HRESULT hr = S_OK;
    auto first = WavSink::Create(path, &hr);
    const std::vector<std::int16_t> pcm = Tone(100);
    first->Write(pcm.data(), pcm.size() * sizeof(std::int16_t));
    first->Close(true);
    const std::size_t size = ReadFileBytes(path).size();

    auto second = WavSink::Create(path, &hr);
    CHECK(second == nullptr);
    CHECK_EQ(hr, HRESULT_FROM_WIN32(ERROR_FILE_EXISTS));
    CHECK_EQ(ReadFileBytes(path).size(), size);                         /* untouched */
}

TEST(WavSink_ReportsAMissingFolder)
{
    TempDir dir;
    HRESULT hr = S_OK;
    auto sink = WavSink::Create(dir.Path(L"no\\such\\folder\\a.wav"), &hr);
    CHECK(sink == nullptr);
    CHECK(FAILED(hr));
}

TEST(WavSink_DiscardingRemovesTheFile)
{
    TempDir dir;
    HRESULT hr = S_OK;
    auto sink = WavSink::Create(dir.Path(L"gone.wav"), &hr);
    CHECK(sink != nullptr);
    CHECK_EQ(dir.Names().size(), std::size_t(1));
    sink->Close(false);
    CHECK(dir.Names().empty());
}

TEST(WavSink_StopsAtTheFormatLimitWithAValidFile)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"limit.wav");
    HRESULT hr = S_OK;
    auto sink = WavSink::Create(path, &hr, 1000);                       /* a tiny "4 GiB" */
    const std::vector<std::int16_t> pcm = Tone(150);                    /* 600 bytes      */
    CHECK(SUCCEEDED(sink->Write(pcm.data(), 600)));
    const HRESULT full = sink->Write(pcm.data(), 600);                 /* would pass 1000 */
    CHECK_EQ(full, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    CHECK_EQ(sink->BytesWritten(), std::uint64_t(600));                /* nothing partial */
    CHECK(SUCCEEDED(sink->Close(true)));

    const std::vector<std::uint8_t> file = ReadFileBytes(path);
    CHECK_EQ(file.size(), std::size_t(44 + 600));
    CHECK_EQ(Le32(file, 40), std::uint32_t(600));
}

TEST(WavSink_CloseIsRepeatableAndWritesAfterItFail)
{
    TempDir dir;
    HRESULT hr = S_OK;
    auto sink = WavSink::Create(dir.Path(L"x.wav"), &hr);
    CHECK(SUCCEEDED(sink->Close(true)));
    CHECK(SUCCEEDED(sink->Close(true)));
    const std::int16_t sample[2] = {1, 2};
    CHECK(FAILED(sink->Write(sample, sizeof(sample))));
}

/* ---- MP3 ---- */

TEST(Mp3Sink_EncodesAFileWhereTheEncoderExists)
{
    if (!Mp3Sink::Supported()) {
        std::printf("    (skipped: Media Foundation is not installed)\n");
        return;
    }
    TempDir dir;
    const std::wstring path = dir.Path(L"a.mp3");
    HRESULT hr = S_OK;
    auto sink = Mp3Sink::Create(path, 128, &hr);
    if (!sink) {
        std::printf("    (skipped: no MP3 encoder here, hr=0x%08lX)\n", static_cast<unsigned long>(hr));
        return;
    }

    const std::vector<std::int16_t> pcm = Tone(48000);                  /* one second */
    for (int i = 0; i < 2; ++i)
        CHECK(SUCCEEDED(sink->Write(pcm.data(), pcm.size() * sizeof(std::int16_t))));
    const HRESULT closed = sink->Close(true);
    if (FAILED(closed))
        std::printf("    Mp3Sink::Close failed, hr=0x%08lX\n", static_cast<unsigned long>(closed));
    CHECK(SUCCEEDED(closed));

    const std::vector<std::uint8_t> file = ReadFileBytes(path);
    CHECK(file.size() > 20000 && file.size() < 40000);                  /* 2 s at 128 kbps is ~32 KB */
    CHECK(std::memcmp(file.data(), "ID3", 3) == 0 || (file[0] == 0xFF && (file[1] & 0xE0) == 0xE0));
    CHECK_EQ(sink->BytesWritten(), std::uint64_t(file.size()));

    HRESULT again = S_OK;
    CHECK(Mp3Sink::Create(path, 128, &again) == nullptr);              /* never overwrites */
    CHECK_EQ(again, HRESULT_FROM_WIN32(ERROR_FILE_EXISTS));
}

TEST(Mp3Sink_DiscardingRemovesTheFile)
{
    if (!Mp3Sink::Supported())
        return;
    TempDir dir;
    HRESULT hr = S_OK;
    auto sink = Mp3Sink::Create(dir.Path(L"x.mp3"), 128, &hr);
    if (!sink)
        return;
    const std::vector<std::int16_t> pcm = Tone(4800);
    sink->Write(pcm.data(), pcm.size() * sizeof(std::int16_t));
    sink->Close(false);
    CHECK(dir.Names().empty());
}

/* ---- silence filler ---- */

TEST(SilenceFiller_PadsAnIdleStreamUpToTheClock)
{
    SilenceFiller filler;
    CHECK_EQ(filler.Owed(0.010, 0), std::size_t(0));                    /* 10 ms behind: normal */
    CHECK_EQ(filler.Owed(0.049, 0), std::size_t(0));
    const std::size_t owed = filler.Owed(1.0, 0);                       /* a whole second idle  */
    CHECK_EQ(owed, std::size_t(48000));
    CHECK_EQ(filler.Owed(1.0, 0), std::size_t(0));                      /* caught up            */
}

TEST(SilenceFiller_CountsRealAudioAgainstTheClock)
{
    SilenceFiller filler;
    CHECK_EQ(filler.Owed(0.5, 24000), std::size_t(0));                  /* half a second delivered */
    CHECK_EQ(filler.Owed(1.0, 12000), std::size_t(12000));              /* a quarter second short  */
    CHECK_EQ(filler.Owed(2.0, 48000), std::size_t(0));                  /* then keeping pace       */
}

TEST(SilenceFiller_NeverPadsAStreamThatIsAhead)
{
    SilenceFiller filler;
    CHECK_EQ(filler.Owed(0.1, 9600), std::size_t(0));                   /* device clock a bit fast */
    CHECK_EQ(filler.Owed(0.2, 0), std::size_t(0));                      /* still ahead             */
}

TEST(SilenceFiller_TreatsAHugeGapAsAClockJump)
{
    SilenceFiller filler;
    CHECK_EQ(filler.Owed(1.9, 0), std::size_t(91200));                  /* a long stall is still padded */
    CHECK_EQ(filler.Owed(1000.0, 0), std::size_t(0));                   /* the PC slept: no 16 minutes of zeros */
    CHECK_EQ(filler.Owed(1000.5, 0), std::size_t(24000));               /* and it counts from there */
}

TEST(SilenceFiller_PadsALateStartInFrontOfTheFirstAudio)
{
    SilenceFiller filler;
    std::vector<std::int16_t> poll(480 * 2, 7);                         /* 10 ms of audio, just arrived */
    filler.Pad(0.65, &poll, 0);                                         /* the device took 0.64 s to start */
    const std::size_t silent = 30720;                                   /* 0.64 s */
    CHECK_EQ(poll.size(), (silent + 480) * 2);
    CHECK_EQ(poll.front(), std::int16_t(0));
    CHECK_EQ(poll[silent * 2 - 1], std::int16_t(0));
    CHECK_EQ(poll[silent * 2], std::int16_t(7));                        /* the audio follows the silence */
    CHECK_EQ(poll.back(), std::int16_t(7));
}

TEST(SilenceFiller_PadLeavesWhatIsBeforeThePollAlone)
{
    SilenceFiller filler;
    std::vector<std::int16_t> out = {1, 2, 3, 4};                       /* an earlier poll's audio */
    out.insert(out.end(), 960 * 2, 9);                                  /* this poll: 20 ms */
    filler.Pad(1.0, &out, 4);                                           /* a second has passed in all */
    CHECK_EQ(out.size(), std::size_t(4 + 48000 * 2));
    CHECK_EQ(out[3], std::int16_t(4));
    CHECK_EQ(out[4], std::int16_t(0));                                  /* silence goes between them */
    CHECK_EQ(out.back(), std::int16_t(9));
}

TEST(SilenceFiller_TheFirstGapMayBeLongerThanALaterOne)
{
    SilenceFiller slowStart;
    CHECK_EQ(slowStart.Owed(20.0, 0), std::size_t(960000));             /* a device that took 20 s to start */

    SilenceFiller stall;
    CHECK_EQ(stall.Owed(0.5, 24000), std::size_t(0));
    CHECK_EQ(stall.Owed(20.5, 0), std::size_t(0));                      /* a later stall that long is a clock jump */

    SilenceFiller hopeless;
    CHECK_EQ(hopeless.Owed(40.0, 0), std::size_t(0));                   /* past 30 s even a start gap is a jump */
}

/* The bug the hardware tests found: a loopback starts at once, a Bluetooth microphone takes most of a
 * second, and each measured time from its own start, so the loopback track came out about 0.6 s longer.
 * With the clock shared, the microphone begins with the silence it missed and the tracks match. */
TEST(Streams_ASlowStartingMicrophoneStaysInStepWithTheLoopback)
{
    SilenceFiller loopbackFiller, micFiller;
    std::size_t loopbackFrames = 0;
    std::vector<std::int16_t> mic;
    const double micStartsAt = 0.65;
    for (int step = 1; step <= 500; ++step) {                           /* 5 s of 10 ms polls */
        const double now = step * 0.010;
        std::vector<std::int16_t> idle;                                 /* the loopback: nothing playing */
        loopbackFiller.Pad(now, &idle, 0);
        loopbackFrames += idle.size() / 2;
        if (now >= micStartsAt) {                                       /* the microphone: audio from 0.65 s */
            std::vector<std::int16_t> poll(480 * 2, 5);
            micFiller.Pad(now, &poll, 0);
            mic.insert(mic.end(), poll.begin(), poll.end());
        }
    }
    const double micFrames = static_cast<double>(mic.size() / 2);
    CHECK_NEAR(micFrames / 48000.0, 5.0, 0.05);                         /* both the length of the recording */
    CHECK_NEAR(loopbackFrames / 48000.0, 5.0, 0.05);
    CHECK_NEAR(micFrames, static_cast<double>(loopbackFrames), 0.05 * 48000);

    std::size_t leading = 0;                                            /* and the microphone starts silent */
    while (leading < mic.size() && mic[leading] == 0)
        ++leading;
    CHECK_NEAR(static_cast<double>(leading / 2) / 48000.0, 0.64, 0.02);
}

/* ---- session clock ---- */

TEST(SessionClock_TheEarliestClaimWins)
{
    SessionClock clock;
    CHECK_EQ(clock.Claim(500), 500LL);
    CHECK_EQ(clock.Claim(900), 500LL);                                  /* a later stream joins the same origin */
    CHECK_EQ(clock.Claim(300), 300LL);                                  /* one that began earlier moves it back */
    CHECK_EQ(clock.Claim(400), 300LL);
}

TEST(SessionClock_ConcurrentClaimsSettleOnTheEarliest)
{
    SessionClock clock;
    std::vector<std::thread> threads;
    for (long long ticks = 1000; ticks >= 100; ticks -= 100)
        threads.emplace_back([&clock, ticks] { clock.Claim(ticks); });
    for (std::thread &t : threads)
        t.join();
    CHECK_EQ(clock.Claim(5000), 100LL);
}

/* ---- stop signal ---- */

TEST(StopSignal_TheTestsNeverTouchTheRealStopEvent)
{
    /* The real name is the one audior shares; run_tests.exe uses a name of its own (test_main.cpp), because a
     * test that signals the real one stops whatever is truly recording, as a hardware test once found out. */
    CHECK_EQ(std::wstring(kStopEventName), std::wstring(L"Local\\AudiorStopEvent"));
    CHECK(std::wstring(StopEventName()) != kStopEventName);
}

TEST(StopSignal_ReachesTheRecordingFromAnotherPlace)
{
    CHECK(!RequestStop());                                              /* nothing to stop: the name is the tests' own */

    StopSignal signal;
    CHECK(signal.Create());
    CHECK(!signal.Raised());
    CHECK(RequestStop());
    CHECK(signal.Raised());

    signal.Close();
    CHECK(!RequestStop());                                              /* gone again */
}

TEST(StopSignal_CreateClearsAStaleSignal)
{
    StopSignal first;
    CHECK(first.Create());
    CHECK(RequestStop());
    CHECK(first.Raised());

    StopSignal second;                                                  /* a new recording starts */
    CHECK(second.Create());
    CHECK(!second.Raised());
}

/* ---- error text ---- */

TEST(WithHr_AddsReasonsForTheFailuresAUserCanFix)
{
    CHECK_EQ(WithHr(L"Failed to write audio data", static_cast<long>(0x80070070)),
             std::wstring(L"Failed to write audio data: the disk is full (hr=0x80070070)"));
    CHECK_EQ(WithHr(L"Audio capture failed", static_cast<long>(0x88890004)),
             std::wstring(L"Audio capture failed: the device was unplugged or switched off (hr=0x88890004)"));
    CHECK_EQ(WithHr(L"Something", static_cast<long>(0x80004005)), std::wstring(L"Something (hr=0x80004005)"));
}
