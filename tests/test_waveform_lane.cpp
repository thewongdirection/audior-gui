#include <vector>

#include "../src/core/waveform_lane.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

PeakBin Bin(float amp)
{
    PeakBin b;
    b.low = -amp;
    b.high = amp;
    return b;
}

std::vector<PeakBin> Bins(std::size_t count, float amp)
{
    return std::vector<PeakBin>(count, Bin(amp));
}

void Feed(WaveformLane &lane, const std::vector<PeakBin> &bins)
{
    lane.Feed(bins.data(), bins.size());
}

constexpr double kBin = 1.0 / kPeakBinsPerSecond;       /* one bin of real time */

}  // namespace

TEST(AmplitudeToHeight_UsesADbScale)
{
    CHECK_NEAR(AmplitudeToHeight(0.0f), 0.0, 0.0);
    CHECK_NEAR(AmplitudeToHeight(1.0f), 1.0, 1e-6);
    CHECK_NEAR(AmplitudeToHeight(0.5f), (54.0 - 6.0206) / 54.0, 1e-4);
    CHECK_NEAR(AmplitudeToHeight(0.001f), 0.0, 0.0);        /* below the -54 dB floor */
    CHECK_NEAR(AmplitudeToHeight(-0.5f), AmplitudeToHeight(0.5f), 1e-6);
    CHECK_NEAR(AmplitudeToHeight(4.0f), 1.0, 0.0);          /* never taller than the lane */
}

TEST(Lane_PlayheadNeverPassesTheData)
{
    WaveformLane lane;
    lane.Advance(1.0);
    CHECK_NEAR(lane.Playhead(), 0.0, 0.0);

    Feed(lane, Bins(50, 0.5f));
    lane.Advance(1.0);
    CHECK_NEAR(lane.Playhead(), 50.0, 1e-9);
    lane.Advance(5.0);
    CHECK_NEAR(lane.Playhead(), 50.0, 1e-9);
}

TEST(Lane_PlayheadGlidesAtTheRealTimeRate)
{
    WaveformLane lane;
    Feed(lane, Bins(50, 0.5f));
    lane.Advance(0.25);
    CHECK_NEAR(lane.Playhead(), 25.0, 1e-9);
    lane.Advance(0.1);
    CHECK_NEAR(lane.Playhead(), 35.0, 1e-9);
}

TEST(Lane_SkipsForwardWhenItFallsTooFarBehind)
{
    WaveformLane lane;                      /* allows 0.6 s of lag */
    Feed(lane, Bins(1000, 0.5f));
    lane.Advance(kBin);
    CHECK_NEAR(lane.Playhead(), 940.0, 1e-9);
}

TEST(Lane_BurstyDeliveryScrollsWithoutStalling)
{
    /* Audio arrives in 25-bin bursts every 250 ms (like audior's flush). */
    WaveformLane lane;
    double lastHead = 0.0;
    for (int tick = 1; tick <= 240; ++tick) {           /* 4 s at 60 ticks/s */
        if (tick % 15 == 0)
            Feed(lane, Bins(25, 0.4f));
        lane.Advance(1.0 / 60.0);
        CHECK(lane.Playhead() >= lastHead);             /* never backwards */
        if (tick > 30)
            CHECK(lane.Playhead() > lastHead);          /* and never frozen */
        lastHead = lane.Playhead();
    }
}

TEST(Lane_ZeroOrNegativeTimeDoesNotMoveThePlayhead)
{
    WaveformLane lane;
    Feed(lane, Bins(50, 0.5f));
    lane.Advance(0.0);
    lane.Advance(-1.0);
    CHECK_NEAR(lane.Playhead(), 0.0, 0.0);
}

TEST(Lane_BarsPlaceAudioInTheRightBar)
{
    WaveformLane lane;                      /* 6 bins per bar */
    std::vector<PeakBin> bins = Bins(60, 0.0f);
    for (int i = 12; i < 18; ++i)           /* exactly bar 2 */
        bins[i] = Bin(0.5f);
    Feed(lane, bins);
    lane.Advance(10.0);

    std::vector<float> h;
    const double phase = lane.Bars(10, &h);
    CHECK_NEAR(phase, 0.0, 1e-9);           /* playhead sits on a bar boundary */
    CHECK_EQ(h.size(), std::size_t(10));
    for (std::size_t i = 0; i < h.size(); ++i) {
        /* Bars 1..10 are shown, oldest first, so bar 2 is at index 1. */
        if (i == 1)
            CHECK_NEAR(h[i], AmplitudeToHeight(0.5f), 1e-6);
        else
            CHECK_NEAR(h[i], 0.0, 0.0);
    }
}

TEST(Lane_BarsKeepTheirHeightAsTheyScroll)
{
    WaveformLane lane;
    std::vector<PeakBin> bins = Bins(100, 0.0f);
    bins[30] = Bin(0.5f);                   /* one loud bin, inside bar 5 */

    const std::size_t count = 8;
    std::vector<float> h;
    for (int step = 1; step <= 99; ++step) {
        lane.Feed(&bins[step - 1], 1);      /* audio arrives in real time, */
        lane.Advance(kBin);                 /* so the playhead is at `step` */
        lane.Bars(count, &h);

        const std::uint64_t newest = static_cast<std::uint64_t>(step) / 6;
        const bool visible = step > 30 && newest >= 5 && newest - 5 < count;

        int loud = 0;
        for (float v : h) {
            if (v > 0.0f) {
                ++loud;
                CHECK_NEAR(v, AmplitudeToHeight(0.5f), 1e-6);
            }
        }
        CHECK_EQ(loud, visible ? 1 : 0);
        if (visible)
            CHECK_NEAR(h[count - 1 - static_cast<std::size_t>(newest - 5)], AmplitudeToHeight(0.5f), 1e-6);
    }
}

TEST(Lane_NewestBarGrowsAsItIsRevealed)
{
    WaveformLane lane;
    Feed(lane, Bins(30, 0.5f));
    lane.Advance(0.2);                      /* playhead at bin 20: bar 3 is 2/6 revealed */

    std::vector<float> h;
    const double phase = lane.Bars(4, &h);
    CHECK_NEAR(phase, 2.0 / 6.0, 1e-9);
    CHECK(h[3] > 0.0f);                     /* the partial bar already has content */
}

TEST(Lane_BarsBeforeTheStartAreEmpty)
{
    WaveformLane lane;
    Feed(lane, Bins(12, 0.5f));
    lane.Advance(1.0);                      /* bars 0 and 1 complete, bar 2 not begun */

    std::vector<float> h;
    lane.Bars(8, &h);
    for (int i = 0; i <= 4; ++i)
        CHECK_NEAR(h[i], 0.0, 0.0);         /* before the recording began */
    CHECK(h[5] > 0.0f);                     /* bar 0 */
    CHECK(h[6] > 0.0f);                     /* bar 1 */
    CHECK_NEAR(h[7], 0.0, 0.0);             /* bar 2: nothing yet */
}

TEST(Lane_AskingForNoBarsIsHarmless)
{
    WaveformLane lane;
    std::vector<float> h(5, 1.0f);
    lane.Bars(0, &h);
    CHECK(h.empty());
}

TEST(Lane_MaxBarsCoversTheWindow)
{
    WaveformConfig config;
    config.windowSeconds = 12;
    config.binsPerBar = 6;
    CHECK_EQ(WaveformLane(config).MaxBars(), std::size_t(200));     /* 1200 bins / 6 */
}

TEST(Lane_MeterFollowsThePlayheadNotTheArrival)
{
    WaveformLane lane;
    std::vector<PeakBin> bins = Bins(5, 0.0f);
    const std::vector<PeakBin> loud = Bins(5, 1.0f);
    bins.insert(bins.end(), loud.begin(), loud.end());      /* silence, then full scale */
    Feed(lane, bins);

    lane.Advance(4 * kBin);                 /* still inside the silence */
    CHECK(std::isinf(lane.LevelDb()));
    CHECK(!lane.Clipping());

    lane.Advance(3 * kBin);                 /* now past bins 5 and 6 */
    CHECK_NEAR(lane.LevelDb(), 0.0, 1e-3);
    CHECK(lane.Clipping());
}

TEST(Lane_ResetStartsFromSilence)
{
    WaveformLane lane;
    Feed(lane, Bins(100, 1.0f));
    lane.Advance(0.5);
    CHECK(lane.HasAudio());

    lane.Reset();
    CHECK(!lane.HasAudio());
    CHECK_NEAR(lane.Playhead(), 0.0, 0.0);
    CHECK(std::isinf(lane.LevelDb()));
    CHECK(!lane.Clipping());

    std::vector<float> h;
    lane.Bars(6, &h);
    for (float v : h)
        CHECK_NEAR(v, 0.0, 0.0);
}

TEST(Lane_LongRecordingsDoNotGrowWithoutBound)
{
    WaveformConfig config;
    config.windowSeconds = 2;               /* a tiny history: 2 s */
    WaveformLane lane(config);

    /* 60 s of audio, loud only in the first second. */
    for (int second = 0; second < 60; ++second) {
        Feed(lane, Bins(100, second == 0 ? 0.9f : 0.0f));
        lane.Advance(1.0);
    }
    std::vector<float> h;
    lane.Bars(20, &h);                      /* the newest 20 bars: the quiet end */
    for (float v : h)
        CHECK_NEAR(v, 0.0, 0.0);
}

TEST(Lane_IsActiveUntilItHasCaughtUpAndTheMeterIsQuiet)
{
    WaveformLane lane;
    CHECK(!lane.Active());                  /* nothing received */

    Feed(lane, Bins(50, 0.5f));
    CHECK(lane.Active());                   /* the playhead is behind the data */

    lane.Advance(1.0);                      /* caught up; the meter is still counting down */
    CHECK(lane.Active());

    lane.Advance(10.0);                     /* the meter has fallen to silence */
    CHECK(!lane.Active());
}
