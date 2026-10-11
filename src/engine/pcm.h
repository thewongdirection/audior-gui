/*
 * pcm.h - The canonical audio format, and the converter that brings any
 * capture format to it.
 *
 * Whatever a device delivers (8 to 32-bit integers or floats, mono to 7.1, any
 * sample rate), it is converted to 48 kHz, 16-bit, stereo before it is
 * recorded.  That keeps the rings, the mixer and the writers format-agnostic.
 * Everything here is plain C++ so it can be tested without a device.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../core/types.h"

namespace audiorgui {

constexpr std::uint32_t kPcmRate           = 48000;
constexpr std::uint32_t kPcmChannels       = 2;
constexpr std::uint32_t kPcmFrameBytes     = kPcmChannels * sizeof(std::int16_t);      /* 4 */
constexpr std::uint32_t kPcmBytesPerSecond = kPcmRate * kPcmFrameBytes;                /* 192000 */

static_assert(kPcmRate % kPeakBinsPerSecond == 0, "a peak bin must cover whole frames");
constexpr std::uint32_t kFramesPerPeakBin = kPcmRate / kPeakBinsPerSecond;

/* What a capture device delivers. */
struct SourceFormat {
    std::uint32_t sampleRate    = kPcmRate;
    std::uint16_t channels      = 2;
    std::uint16_t bitsPerSample = 16;       /* container size: 8, 16, 24 or 32 */
    bool          isFloat       = false;    /* 32-bit float samples            */
    std::uint32_t channelMask   = 0;        /* speaker positions; 0 = unknown  */
};

class PcmConverter {
public:
    /* False if the format cannot be converted.  `gain` is linear (1 = unchanged);
     * it is applied before the result is clamped to 16 bits. */
    bool Init(const SourceFormat &format, float gain);

    /* Converts `frames` source frames to canonical frames and appends them to
     * `out` (interleaved left, right).  `silent` means the packet is silence
     * and `data` is not read.  State carries over between calls, so packets
     * may be any size. */
    void Convert(const std::uint8_t *data, std::uint32_t frames, bool silent, std::vector<std::int16_t> *out);

private:
    enum class Kind { U8, S16, S24, S32, F32 };
    static constexpr unsigned kMaxChannels = 16;

    void BuildDownmix(const SourceFormat &format);
    void Decode(const std::uint8_t *frame, float *left, float *right) const;

    Kind     kind_        = Kind::S16;
    unsigned channels_    = 2;
    unsigned sampleBytes_ = 2;
    unsigned frameBytes_  = 4;
    float    gain_        = 1.0f;
    float    left_[kMaxChannels]  = {};       /* each channel's weight in the left output  */
    float    right_[kMaxChannels] = {};       /* ... and in the right one                  */

    bool     fastCopy_ = false;               /* 16-bit stereo 48 kHz at unity gain        */
    bool     resample_ = false;
    bool     havePrev_ = false;
    double   step_     = 1.0;                 /* source frames per output frame            */
    double   pos_      = 1.0;                 /* where the next output frame falls, in
                                                 source frames after the previous one
                                                 (0 to 1; 1 = on the current frame)        */
    float    prevL_ = 0.0f, prevR_ = 0.0f;    /* the previous source frame                 */
};

}  // namespace audiorgui
