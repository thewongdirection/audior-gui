#include <algorithm>
#include <cmath>
#include <vector>

#include "../src/engine/speech_tap.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

constexpr double kPi = 3.14159265358979323846;

/* A stereo sine of `hertz` at 48 kHz, `frames` long (the same in both channels). */
std::vector<std::int16_t> Sine(double hertz, std::size_t frames, double amplitude = 10000.0)
{
    std::vector<std::int16_t> stereo(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        const std::int16_t v = static_cast<std::int16_t>(std::lround(amplitude * std::sin(2.0 * kPi * hertz * i / 48000.0)));
        stereo[2 * i] = stereo[2 * i + 1] = v;
    }
    return stereo;
}

std::vector<std::int16_t> Decimate(const std::vector<std::int16_t> &stereo, std::size_t chunkFrames)
{
    SpeechDecimator decimator;
    std::vector<std::int16_t> out;
    for (std::size_t at = 0; at < stereo.size() / 2; at += chunkFrames)
        decimator.Add(stereo.data() + 2 * at, std::min(chunkFrames, stereo.size() / 2 - at), &out);
    return out;
}

/* The largest absolute sample after the filter has settled. */
int Peak(const std::vector<std::int16_t> &samples)
{
    int peak = 0;
    for (std::size_t i = samples.size() / 4; i < samples.size(); ++i)
        peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
    return peak;
}

}  // namespace

TEST(SpeechTap_GivesOneSampleForEveryThreeFrames)
{
    CHECK_EQ(Decimate(Sine(1000, 4800), 4800).size(), std::size_t(1600));
    CHECK_EQ(Decimate(Sine(1000, 4801), 4801).size(), std::size_t(1600));      /* the partial triple waits */
    CHECK_EQ(Decimate(Sine(1000, 2), 2).size(), std::size_t(0));
}

TEST(SpeechTap_PiecesOfAnySizeGiveTheSameAudio)
{
    const std::vector<std::int16_t> input = Sine(440, 9600);
    const std::vector<std::int16_t> whole = Decimate(input, 9600);
    for (std::size_t chunk : {std::size_t(1), std::size_t(7), std::size_t(480), std::size_t(1000)})
        CHECK(Decimate(input, chunk) == whole);
}

TEST(SpeechTap_ConstantLevelsPassUnchanged)
{
    std::vector<std::int16_t> stereo(2 * 960, 12000);
    const std::vector<std::int16_t> out = Decimate(stereo, 960);
    CHECK_EQ(out.size(), std::size_t(320));
    CHECK_EQ(int(out.back()), 12000);
}

TEST(SpeechTap_SpeechFrequenciesKeepTheirLevel)
{
    for (double hertz : {200.0, 1000.0, 3000.0}) {
        const int peak = Peak(Decimate(Sine(hertz, 48000), 480));
        CHECK(peak > 9700 && peak < 10300);             /* within about 3% of the 10000 put in */
    }
}

TEST(SpeechTap_NothingAboveTheNewNyquistFoldsBackIn)
{
    /* Without the filter, 10 kHz at a 16 kHz rate would turn up as 6 kHz at full strength. */
    for (double hertz : {10000.0, 12000.0, 20000.0}) {
        const int peak = Peak(Decimate(Sine(hertz, 48000), 480));
        CHECK(peak < 100);                              /* more than 40 dB down */
    }
}

TEST(SpeechTap_ChannelsAreAveraged)
{
    std::vector<std::int16_t> stereo;
    for (int i = 0; i < 960; ++i) {
        stereo.push_back(10000);                        /* left */
        stereo.push_back(-10000);                       /* right: the opposite */
    }
    CHECK_EQ(int(Decimate(stereo, 960).back()), 0);

    for (int i = 0; i < 960; ++i) {
        stereo[2 * i] = 8000;
        stereo[2 * i + 1] = 0;                          /* one channel silent */
    }
    CHECK_EQ(int(Decimate(stereo, 960).back()), 4000);
}

TEST(SpeechTap_SilenceStaysSilent)
{
    const std::vector<std::int16_t> out = Decimate(std::vector<std::int16_t>(2 * 4800, 0), 480);
    CHECK_EQ(out.size(), std::size_t(1600));
    CHECK_EQ(Peak(out), 0);
}

TEST(SpeechTap_FullScaleDoesNotWrapAround)
{
    std::vector<std::int16_t> loud(2 * 4800, 32767);
    for (std::size_t i = 2 * 100; i < loud.size(); i += 2 * 3 * 5)  /* with some sharp edges in it */
        loud[i] = loud[i + 1] = -32768;
    const std::vector<std::int16_t> out = Decimate(loud, 480);
    for (std::size_t i = 100; i < out.size(); ++i)      /* past the filter's start-up: the edges dent the level but a */
        CHECK(out[i] > 0);                              /* missing clamp would wrap it to a large negative            */

    const std::vector<std::int16_t> steady = Decimate(std::vector<std::int16_t>(2 * 2400, 32767), 2400);
    CHECK(steady.back() >= 32766);                      /* full scale stays full scale: no sign flip */
}
