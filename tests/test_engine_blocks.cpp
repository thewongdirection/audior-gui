/* Tests of the engine's small parts: ring buffer, peak tap and queue, mixer. */
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include "../src/engine/mixer.h"
#include "../src/engine/peak_tap.h"
#include "../src/engine/ring_buffer.h"
#include "test_support.h"

using namespace audiorgui;

/* ---- RingBuffer ---- */

TEST(Ring_WritesAndReadsInOrder)
{
    RingBuffer ring(16);
    const std::uint8_t in[] = {1, 2, 3, 4, 5};
    CHECK_EQ(ring.Write(in, 5), std::size_t(5));
    CHECK_EQ(ring.Used(), std::size_t(5));

    std::uint8_t out[8] = {};
    CHECK_EQ(ring.Read(out, 3), std::size_t(3));
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);
    CHECK_EQ(ring.Used(), std::size_t(2));
    CHECK_EQ(ring.Read(out, 8), std::size_t(2));
    CHECK(out[0] == 4 && out[1] == 5);
    CHECK_EQ(ring.Read(out, 8), std::size_t(0));                  /* empty */
}

TEST(Ring_WrapsAroundTheEnd)
{
    RingBuffer ring(8);
    std::uint8_t scratch[8];
    const std::uint8_t first[] = {1, 2, 3, 4, 5, 6};
    ring.Write(first, 6);
    ring.Read(scratch, 6);                                         /* head is now at 6 */

    const std::uint8_t second[] = {10, 11, 12, 13, 14};            /* wraps past the end */
    CHECK_EQ(ring.Write(second, 5), std::size_t(5));
    std::uint8_t out[8] = {};
    CHECK_EQ(ring.Read(out, 8), std::size_t(5));
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(out[i], static_cast<std::uint8_t>(10 + i));
}

TEST(Ring_AFullBufferTakesOnlyWhatFits)
{
    RingBuffer ring(4);
    const std::uint8_t in[] = {1, 2, 3, 4, 5, 6};
    CHECK_EQ(ring.Write(in, 6), std::size_t(4));
    CHECK_EQ(ring.Write(in, 1), std::size_t(0));
    std::uint8_t out[4];
    ring.Read(out, 4);
    CHECK(out[0] == 1 && out[3] == 4);                            /* the oldest bytes were kept */
}

TEST(Ring_CarriesAStreamBetweenThreadsIntact)
{
    RingBuffer ring(4096);
    const std::size_t total = 2000000;
    std::atomic<bool> ok{true};

    std::thread producer([&] {
        std::uint8_t chunk[977];
        std::size_t sent = 0;
        while (sent < total) {
            const std::size_t n = std::min(sizeof(chunk), total - sent);
            for (std::size_t i = 0; i < n; ++i)
                chunk[i] = static_cast<std::uint8_t>((sent + i) % 251);
            std::size_t done = 0;
            while (done < n) {
                const std::size_t w = ring.Write(chunk + done, n - done);
                done += w;
                if (w == 0)
                    std::this_thread::yield();
            }
            sent += n;
        }
    });

    std::size_t received = 0;
    std::uint8_t buf[1500];
    while (received < total) {
        const std::size_t n = ring.Read(buf, sizeof(buf));
        for (std::size_t i = 0; i < n; ++i)
            ok = ok && buf[i] == static_cast<std::uint8_t>((received + i) % 251);
        received += n;
        if (n == 0)
            std::this_thread::yield();
    }
    producer.join();
    CHECK(ok);
    CHECK_EQ(ring.Used(), std::size_t(0));
}

/* ---- PeakAccumulator / PeakQueue ---- */

namespace {

std::vector<std::int16_t> Constant(std::size_t frames, std::int16_t left, std::int16_t right)
{
    std::vector<std::int16_t> v;
    for (std::size_t i = 0; i < frames; ++i) {
        v.push_back(left);
        v.push_back(right);
    }
    return v;
}

}  // namespace

TEST(PeakTap_OneBinPerTenMilliseconds)
{
    PeakAccumulator acc;
    std::vector<PeakBin> bins;
    const auto audio = Constant(48000, 1000, -2000);               /* one second */
    acc.Add(audio.data(), 48000, &bins);
    CHECK_EQ(bins.size(), std::size_t(kPeakBinsPerSecond));
    CHECK_NEAR(bins[0].high, 1000.0 / 32768.0, 1e-6);
    CHECK_NEAR(bins[0].low, -2000.0 / 32768.0, 1e-6);
}

TEST(PeakTap_APartBinCarriesOverBetweenCalls)
{
    PeakAccumulator acc;
    std::vector<PeakBin> bins;
    const auto small = Constant(100, 500, 500);
    acc.Add(small.data(), 100, &bins);
    CHECK_EQ(bins.size(), std::size_t(0));

    std::vector<std::int16_t> rest = Constant(380, 0, 0);
    rest[10] = 9000;                                               /* a peak in the second part */
    acc.Add(rest.data(), 380, &bins);
    CHECK_EQ(bins.size(), std::size_t(1));
    CHECK_NEAR(bins[0].high, 9000.0 / 32768.0, 1e-6);
}

TEST(PeakTap_FullScaleReadsAsOne)
{
    PeakAccumulator acc;
    std::vector<PeakBin> bins;
    const auto audio = Constant(kFramesPerPeakBin, -32768, 32767);
    acc.Add(audio.data(), kFramesPerPeakBin, &bins);
    CHECK_EQ(bins.size(), std::size_t(1));
    CHECK_NEAR(bins[0].low, -1.0, 0.0);
    CHECK(bins[0].high > 0.9999f && bins[0].high < 1.0f);
}

TEST(PeakQueue_DeliversInOrderAndOnlyOnce)
{
    PeakQueue q(8);
    PeakBin in[3];
    for (int i = 0; i < 3; ++i)
        in[i].high = static_cast<float>(i + 1);
    q.Push(in, 3);

    PeakBin out[8];
    CHECK_EQ(q.Take(out, 2), std::size_t(2));
    CHECK(out[0].high == 1.0f && out[1].high == 2.0f);
    CHECK_EQ(q.Take(out, 8), std::size_t(1));
    CHECK(out[0].high == 3.0f);
    CHECK_EQ(q.Take(out, 8), std::size_t(0));
}

TEST(PeakQueue_KeepsTheNewestWhenNobodyIsListening)
{
    PeakQueue q(4);
    PeakBin in[10];
    for (int i = 0; i < 10; ++i)
        in[i].high = static_cast<float>(i);
    q.Push(in, 10);

    PeakBin out[10];
    CHECK_EQ(q.Take(out, 10), std::size_t(4));
    CHECK(out[0].high == 6.0f && out[3].high == 9.0f);
}

TEST(PeakQueue_ClearForgetsEverything)
{
    PeakQueue q(4);
    PeakBin bin;
    q.Push(&bin, 1);
    q.Clear();
    PeakBin out[1];
    CHECK_EQ(q.Take(out, 1), std::size_t(0));
}

/* ---- Mixer ---- */

TEST(Mixer_WeighsTheTwoStreams)
{
    const std::int16_t a[] = {1000, -1000, 0};
    const std::int16_t b[] = {2000, 2000, 2000};
    std::int16_t out[3];
    MixWeighted(out, a, b, 3, 0.65f);
    CHECK_EQ(out[0], std::int16_t(1350));                           /* 0.65 * 1000 + 0.35 * 2000 */
    CHECK_EQ(out[1], std::int16_t(50));                             /* -650 + 700 */
    CHECK_EQ(out[2], std::int16_t(700));
}

TEST(Mixer_CannotClipAtAnySplit)
{
    const std::int16_t high[] = {32767};
    const std::int16_t low[] = {-32768};
    for (float w : {0.0f, 0.25f, 0.65f, 1.0f}) {
        std::int16_t out[1];
        MixWeighted(out, high, high, 1, w);
        CHECK_EQ(out[0], std::int16_t(32767));
        MixWeighted(out, low, low, 1, w);
        CHECK_EQ(out[0], std::int16_t(-32768));
    }
}

TEST(Mixer_ExtremeWeightsPickOneStream)
{
    const std::int16_t a[] = {123, -456};
    const std::int16_t b[] = {789, 321};
    std::int16_t out[2];
    MixWeighted(out, a, b, 2, 1.0f);
    CHECK(out[0] == 123 && out[1] == -456);
    MixWeighted(out, a, b, 2, 0.0f);
    CHECK(out[0] == 789 && out[1] == 321);
}

TEST(Mixer_MayWriteOverItsInput)
{
    std::int16_t a[] = {1000, 1000};
    const std::int16_t b[] = {3000, 3000};
    MixWeighted(a, a, b, 2, 0.5f);
    CHECK(a[0] == 2000 && a[1] == 2000);
}
