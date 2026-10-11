#include "pcm.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace audiorgui {

namespace {

/* Speaker positions, as in a WAVEFORMATEXTENSIBLE channel mask. */
constexpr std::uint32_t kFrontLeft = 0x1, kFrontRight = 0x2, kFrontCenter = 0x4, kBackLeft = 0x10,
                        kBackRight = 0x20, kBackCenter = 0x100, kSideLeft = 0x200, kSideRight = 0x400;
constexpr float kSqrtHalf = 0.70710678f;

std::int16_t ToS16(float sample)
{
    float v = sample * 32768.0f;
    if (v != v)                                 /* NaN */
        return 0;
    v = std::min(32767.0f, std::max(-32768.0f, v));
    return static_cast<std::int16_t>(std::lrint(v));
}

unsigned PopCount(std::uint32_t v)
{
    unsigned n = 0;
    for (; v; v &= v - 1)
        ++n;
    return n;
}

}  // namespace

bool PcmConverter::Init(const SourceFormat &f, float gain)
{
    if (f.channels == 0 || f.channels > kMaxChannels || f.sampleRate == 0)
        return false;
    if (f.isFloat) {
        if (f.bitsPerSample != 32)
            return false;
        kind_ = Kind::F32;
    } else {
        switch (f.bitsPerSample) {
        case 8:  kind_ = Kind::U8;  break;
        case 16: kind_ = Kind::S16; break;
        case 24: kind_ = Kind::S24; break;
        case 32: kind_ = Kind::S32; break;
        default: return false;
        }
    }
    channels_    = f.channels;
    sampleBytes_ = f.bitsPerSample / 8u;
    frameBytes_  = channels_ * sampleBytes_;
    gain_        = gain;
    resample_    = f.sampleRate != kPcmRate;
    step_        = static_cast<double>(f.sampleRate) / kPcmRate;
    pos_         = 1.0;
    havePrev_    = false;
    BuildDownmix(f);
    fastCopy_ = kind_ == Kind::S16 && channels_ == 2 && !resample_ && gain == 1.0f;
    return true;
}

/* How much of each source channel goes to the left and the right output.  Mono
 * feeds both; stereo passes through; a surround layout is folded down using
 * its speaker positions and scaled so that it cannot clip. */
void PcmConverter::BuildDownmix(const SourceFormat &f)
{
    std::fill(std::begin(left_), std::end(left_), 0.0f);
    std::fill(std::begin(right_), std::end(right_), 0.0f);

    if (channels_ == 1) {
        left_[0] = right_[0] = 1.0f;
        return;
    }
    if (channels_ == 2 || f.channelMask == 0 || PopCount(f.channelMask) != channels_) {
        left_[0] = 1.0f;                        /* first two channels as left and right */
        right_[1] = 1.0f;
        return;
    }

    unsigned channel = 0;
    for (unsigned bit = 0; bit < 32 && channel < channels_; ++bit) {
        const std::uint32_t speaker = 1u << bit;
        if (!(f.channelMask & speaker))
            continue;
        switch (speaker) {
        case kFrontLeft:   left_[channel] = 1.0f; break;
        case kFrontRight:  right_[channel] = 1.0f; break;
        case kFrontCenter: left_[channel] = right_[channel] = kSqrtHalf; break;
        case kBackLeft:
        case kSideLeft:    left_[channel] = kSqrtHalf; break;
        case kBackRight:
        case kSideRight:   right_[channel] = kSqrtHalf; break;
        case kBackCenter:  left_[channel] = right_[channel] = 0.5f; break;
        default: break;                         /* LFE and the rest are left out */
        }
        ++channel;
    }

    float sumL = 0.0f, sumR = 0.0f;
    for (unsigned c = 0; c < channels_; ++c) {
        sumL += left_[c];
        sumR += right_[c];
    }
    const float peak = std::max(sumL, sumR);
    if (peak <= 0.0f) {                         /* nothing usable in the mask */
        left_[0] = 1.0f;
        right_[1] = 1.0f;
        return;
    }
    for (unsigned c = 0; c < channels_; ++c) {
        left_[c] /= peak;
        right_[c] /= peak;
    }
}

void PcmConverter::Decode(const std::uint8_t *p, float *left, float *right) const
{
    float v[kMaxChannels];
    for (unsigned c = 0; c < channels_; ++c, p += sampleBytes_) {
        switch (kind_) {
        case Kind::U8:
            v[c] = (static_cast<int>(p[0]) - 128) / 128.0f;
            break;
        case Kind::S16: {
            std::int16_t s;
            std::memcpy(&s, p, sizeof(s));
            v[c] = static_cast<float>(s) / 32768.0f;
            break;
        }
        case Kind::S24: {
            const std::uint32_t raw = static_cast<std::uint32_t>(p[0]) << 8 | static_cast<std::uint32_t>(p[1]) << 16 |
                                      static_cast<std::uint32_t>(p[2]) << 24;
            v[c] = static_cast<float>(static_cast<std::int32_t>(raw) >> 8) / 8388608.0f;
            break;
        }
        case Kind::S32: {
            std::int32_t s;
            std::memcpy(&s, p, sizeof(s));
            v[c] = static_cast<float>(s) / 2147483648.0f;
            break;
        }
        case Kind::F32:
            std::memcpy(&v[c], p, sizeof(float));
            break;
        }
    }
    float l = 0.0f, r = 0.0f;
    for (unsigned c = 0; c < channels_; ++c) {
        l += left_[c] * v[c];
        r += right_[c] * v[c];
    }
    *left = l;
    *right = r;
}

void PcmConverter::Convert(const std::uint8_t *data, std::uint32_t frames, bool silent, std::vector<std::int16_t> *out)
{
    if (frames == 0)
        return;

    if (fastCopy_ && !silent) {                 /* already canonical: copy */
        const std::size_t at = out->size();
        out->resize(at + static_cast<std::size_t>(frames) * kPcmChannels);
        std::memcpy(out->data() + at, data, static_cast<std::size_t>(frames) * kPcmFrameBytes);
        return;
    }

    for (std::uint32_t f = 0; f < frames; ++f) {
        float l = 0.0f, r = 0.0f;
        if (!silent)
            Decode(data + static_cast<std::size_t>(f) * frameBytes_, &l, &r);
        l *= gain_;
        r *= gain_;

        if (!resample_) {
            out->push_back(ToS16(l));
            out->push_back(ToS16(r));
            continue;
        }

        /* Linear interpolation between the previous source frame and this one. */
        if (!havePrev_) {
            prevL_ = l;
            prevR_ = r;
            havePrev_ = true;
        }
        while (pos_ <= 1.0) {
            const float a = static_cast<float>(pos_);
            out->push_back(ToS16(prevL_ + (l - prevL_) * a));
            out->push_back(ToS16(prevR_ + (r - prevR_) * a));
            pos_ += step_;
        }
        pos_ -= 1.0;
        prevL_ = l;
        prevR_ = r;
    }
}

}  // namespace audiorgui
