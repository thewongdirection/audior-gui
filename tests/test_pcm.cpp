#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../src/engine/pcm.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

SourceFormat Fmt(unsigned rate, unsigned channels, unsigned bits, bool isFloat = false, unsigned mask = 0)
{
    SourceFormat f;
    f.sampleRate = rate;
    f.channels = static_cast<std::uint16_t>(channels);
    f.bitsPerSample = static_cast<std::uint16_t>(bits);
    f.isFloat = isFloat;
    f.channelMask = mask;
    return f;
}

template <typename T>
std::vector<std::uint8_t> Bytes(const std::vector<T> &values)
{
    std::vector<std::uint8_t> bytes(values.size() * sizeof(T));
    std::memcpy(bytes.data(), values.data(), bytes.size());
    return bytes;
}

/* Converts the whole buffer in one packet. */
std::vector<std::int16_t> Convert(const SourceFormat &f, float gain, const std::vector<std::uint8_t> &data,
                                  std::uint32_t frames, bool silent = false)
{
    PcmConverter c;
    CHECK(c.Init(f, gain));
    std::vector<std::int16_t> out;
    c.Convert(data.data(), frames, silent, &out);
    return out;
}

/* 16-bit stereo ramp: left = i, right = -i. */
std::vector<std::int16_t> Ramp(std::size_t frames)
{
    std::vector<std::int16_t> v;
    for (std::size_t i = 0; i < frames; ++i) {
        v.push_back(static_cast<std::int16_t>(i));
        v.push_back(static_cast<std::int16_t>(-static_cast<int>(i)));
    }
    return v;
}

/* A mono sine at `hz`, sampled at `rate`, peaking at `amplitude` of full scale. */
std::vector<std::int16_t> Sine(unsigned rate, double hz, double amplitude, std::size_t frames)
{
    std::vector<std::int16_t> v;
    for (std::size_t i = 0; i < frames; ++i)
        v.push_back(static_cast<std::int16_t>(std::lround(32767.0 * amplitude *
                                                          std::sin(2.0 * 3.14159265358979 * hz * static_cast<double>(i) / rate))));
    return v;
}

}  // namespace

TEST(Pcm_RejectsFormatsItCannotDecode)
{
    PcmConverter c;
    CHECK(!c.Init(Fmt(48000, 0, 16), 1.0f));
    CHECK(!c.Init(Fmt(48000, 17, 16), 1.0f));
    CHECK(!c.Init(Fmt(48000, 2, 12), 1.0f));
    CHECK(!c.Init(Fmt(48000, 2, 16, true), 1.0f));           /* float must be 32-bit */
    CHECK(!c.Init(Fmt(0, 2, 16), 1.0f));
    CHECK(c.Init(Fmt(48000, 2, 32, true), 1.0f));
}

TEST(Pcm_CanonicalInputPassesThroughExactlyInAnyPacketSizes)
{
    const std::vector<std::int16_t> source = Ramp(1000);
    const std::vector<std::uint8_t> bytes = Bytes(source);

    PcmConverter c;
    CHECK(c.Init(Fmt(48000, 2, 16), 1.0f));
    std::vector<std::int16_t> out;
    const std::uint32_t packets[] = {1, 7, 100, 892};
    std::size_t at = 0;
    for (std::uint32_t frames : packets) {
        c.Convert(bytes.data() + at * kPcmFrameBytes, frames, false, &out);
        at += frames;
    }
    CHECK(out == source);
}

TEST(Pcm_FloatStereoIsExactToo)
{
    std::vector<float> floats;
    for (int i = -100; i <= 100; ++i) {
        floats.push_back(static_cast<float>(i * 300) / 32768.0f);
        floats.push_back(static_cast<float>(-i * 300) / 32768.0f);
    }
    const auto out = Convert(Fmt(48000, 2, 32, true), 1.0f, Bytes(floats), 201);
    CHECK_EQ(out.size(), std::size_t(402));
    for (int i = -100; i <= 100; ++i) {
        CHECK_EQ(out[(i + 100) * 2], static_cast<std::int16_t>(i * 300));
        CHECK_EQ(out[(i + 100) * 2 + 1], static_cast<std::int16_t>(-i * 300));
    }
}

TEST(Pcm_MonoFeedsBothChannels)
{
    const std::vector<std::int16_t> mono = {100, -200, 300};
    const auto out = Convert(Fmt(48000, 1, 16), 1.0f, Bytes(mono), 3);
    const std::vector<std::int16_t> expect = {100, 100, -200, -200, 300, 300};
    CHECK(out == expect);
}

TEST(Pcm_SurroundIsFoldedDownWithoutClipping)
{
    /* 5.1: FL FR FC LFE BL BR (mask 0x3F).  Left gets FL + 0.707 FC + 0.707 BL,
     * scaled by 1 / 2.414 so that a full-scale input cannot clip. */
    const SourceFormat f = Fmt(48000, 6, 16, false, 0x3F);
    auto frame = [](std::int16_t fl, std::int16_t fr, std::int16_t fc, std::int16_t lfe, std::int16_t bl, std::int16_t br) {
        return std::vector<std::int16_t>{fl, fr, fc, lfe, bl, br};
    };

    auto out = Convert(f, 1.0f, Bytes(frame(20000, 0, 0, 0, 0, 0)), 1);
    CHECK_NEAR(out[0], 20000 * 0.41421356, 1.0);               /* front left: left only */
    CHECK_NEAR(out[1], 0.0, 0.0);

    out = Convert(f, 1.0f, Bytes(frame(0, 0, 20000, 0, 0, 0)), 1);
    CHECK_NEAR(out[0], 20000 * 0.29289322, 1.0);               /* centre: both */
    CHECK_NEAR(out[1], 20000 * 0.29289322, 1.0);

    out = Convert(f, 1.0f, Bytes(frame(0, 0, 0, 30000, 0, 0)), 1);
    CHECK_NEAR(out[0], 0.0, 0.0);                              /* the LFE is left out */
    CHECK_NEAR(out[1], 0.0, 0.0);

    out = Convert(f, 1.0f, Bytes(frame(32767, 32767, 32767, 0, 32767, 32767)), 1);
    CHECK(out[0] >= 32766 && out[1] >= 32766);                 /* everything at full scale: full scale, no wrap */
}

TEST(Pcm_SurroundWithoutAUsableMaskUsesTheFirstTwoChannels)
{
    const std::vector<std::int16_t> frame = {111, 222, 333, 444, 555, 666};
    const auto out = Convert(Fmt(48000, 6, 16), 1.0f, Bytes(frame), 1);
    CHECK_EQ(out[0], std::int16_t(111));
    CHECK_EQ(out[1], std::int16_t(222));
}

TEST(Pcm_DecodesEveryIntegerWidth)
{
    /* 8-bit is unsigned, centred on 128. */
    auto out = Convert(Fmt(48000, 1, 8), 1.0f, std::vector<std::uint8_t>{128, 0, 255}, 3);
    CHECK_EQ(out[0], std::int16_t(0));
    CHECK_EQ(out[2], std::int16_t(-32768));
    CHECK_EQ(out[4], std::int16_t(32512));

    /* 24-bit, packed little-endian: +0.5 and -0.5 of full scale. */
    out = Convert(Fmt(48000, 1, 24), 1.0f, std::vector<std::uint8_t>{0x00, 0x00, 0x40, 0x00, 0x00, 0xC0}, 2);
    CHECK_EQ(out[0], std::int16_t(16384));
    CHECK_EQ(out[2], std::int16_t(-16384));

    /* 32-bit. */
    const std::vector<std::int32_t> wide = {0x40000000, static_cast<std::int32_t>(0x80000000u)};
    out = Convert(Fmt(48000, 1, 32), 1.0f, Bytes(wide), 2);
    CHECK_EQ(out[0], std::int16_t(16384));
    CHECK_EQ(out[2], std::int16_t(-32768));
}

TEST(Pcm_FloatsAreClampedAndNaNIsSilence)
{
    const std::vector<float> floats = {0.5f, 2.0f, -2.0f, std::nanf("")};
    const auto out = Convert(Fmt(48000, 1, 32, true), 1.0f, Bytes(floats), 4);
    CHECK_EQ(out[0], std::int16_t(16384));
    CHECK_EQ(out[2], std::int16_t(32767));
    CHECK_EQ(out[4], std::int16_t(-32768));
    CHECK_EQ(out[6], std::int16_t(0));
}

TEST(Pcm_GainIsExactAtTwoAndClipsAtFour)
{
    const std::vector<std::int16_t> mono = {1000, -1000, 20000, -20000};
    const SourceFormat f = Fmt(48000, 1, 16);

    auto out = Convert(f, 2.0f, Bytes(mono), 4);                /* +100% */
    CHECK_EQ(out[0], std::int16_t(2000));
    CHECK_EQ(out[2], std::int16_t(-2000));
    CHECK_EQ(out[4], std::int16_t(32767));                      /* 40000 clips */
    CHECK_EQ(out[6], std::int16_t(-32768));

    out = Convert(f, 4.0f, Bytes(mono), 4);                     /* +300% */
    CHECK_EQ(out[0], std::int16_t(4000));
    CHECK_EQ(out[4], std::int16_t(32767));
}

TEST(Pcm_GainAlsoAppliesToCanonicalStereo)
{
    const auto out = Convert(Fmt(48000, 2, 16), 2.0f, Bytes(Ramp(10)), 10);
    CHECK_EQ(out[2 * 5], std::int16_t(10));
    CHECK_EQ(out[2 * 5 + 1], std::int16_t(-10));
}

TEST(Pcm_SilentPacketsProduceSilenceWithoutReadingTheData)
{
    PcmConverter c;
    CHECK(c.Init(Fmt(48000, 2, 16), 1.0f));
    std::vector<std::int16_t> out;
    c.Convert(nullptr, 100, true, &out);                        /* the data pointer must not be touched */
    CHECK_EQ(out.size(), std::size_t(200));
    for (std::int16_t s : out)
        CHECK_EQ(s, std::int16_t(0));
}

TEST(Pcm_ResamplingKeepsTheFrequencyAndTheLevel)
{
    for (unsigned rate : {44100u, 32000u, 16000u, 96000u}) {
        PcmConverter c;
        CHECK(c.Init(Fmt(rate, 1, 16), 1.0f));
        const auto source = Sine(rate, 1000.0, 0.5, rate);      /* one second */
        const auto bytes = Bytes(source);
        std::vector<std::int16_t> out;
        c.Convert(bytes.data(), static_cast<std::uint32_t>(source.size()), false, &out);

        const std::size_t frames = out.size() / 2;
        CHECK(frames >= 47990 && frames <= 48010);              /* one second at 48 kHz */

        int crossings = 0;
        int peak = 0;
        for (std::size_t i = 1; i < frames; ++i) {
            if ((out[(i - 1) * 2] < 0) != (out[i * 2] < 0))
                ++crossings;
            peak = std::max(peak, std::abs(static_cast<int>(out[i * 2])));
        }
        CHECK_NEAR(crossings / 2.0, 1000.0, 10.0);              /* 1 kHz, within 1% */
        CHECK_NEAR(peak, 16383, 16383 * 0.04);                  /* amplitude within 4% (linear interpolation) */
    }
}

TEST(Pcm_DownsamplingByTwoPicksEveryOtherFrame)
{
    const auto source = Ramp(2000);
    PcmConverter c;
    CHECK(c.Init(Fmt(96000, 2, 16), 1.0f));
    std::vector<std::int16_t> out;
    c.Convert(Bytes(source).data(), 2000, false, &out);
    CHECK_EQ(out.size(), std::size_t(2000));                    /* 1000 frames */
    for (std::size_t k = 0; k < 1000; ++k)
        CHECK_EQ(out[k * 2], source[k * 2 * 2]);
}

TEST(Pcm_PacketBoundariesAreSeamlessWhenResampling)
{
    const auto source = Sine(44100, 440.0, 0.6, 44100);
    const auto bytes = Bytes(source);

    PcmConverter whole, split;
    CHECK(whole.Init(Fmt(44100, 1, 16), 1.0f));
    CHECK(split.Init(Fmt(44100, 1, 16), 1.0f));

    std::vector<std::int16_t> a, b;
    whole.Convert(bytes.data(), 44100, false, &a);

    const std::uint32_t packets[] = {1, 441, 10, 3333, 17000, 44100 - 1 - 441 - 10 - 3333 - 17000};
    std::size_t at = 0;
    for (std::uint32_t n : packets) {
        split.Convert(bytes.data() + at * 2, n, false, &b);
        at += n;
    }
    CHECK(a == b);
}

TEST(Pcm_ASilentPacketWhenResamplingHasTheRightLength)
{
    PcmConverter c;
    CHECK(c.Init(Fmt(44100, 2, 16), 1.0f));
    std::vector<std::int16_t> out;
    c.Convert(nullptr, 44100, true, &out);
    CHECK(out.size() / 2 >= 47990 && out.size() / 2 <= 48010);
    for (std::int16_t s : out)
        CHECK_EQ(s, std::int16_t(0));
}
