#include "voice_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace audiorgui {

namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr std::size_t kSize = StereoStft::kSize;
constexpr std::size_t kHop  = StereoStft::kHop;
constexpr std::size_t kBins = StereoStft::kBins;

double DbToLinear(double db) { return std::pow(10.0, db / 20.0); }

}  // namespace

/* ---- BiquadCascade ------------------------------------------------------------------------------------------------ */

void BiquadCascade::AddHighPass(double hz, double q)
{
    const double w = 2.0 * kPi * hz / kVoiceRate, cs = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    sections_.push_back({(1.0 + cs) / 2.0 / a0, -(1.0 + cs) / a0, (1.0 + cs) / 2.0 / a0, -2.0 * cs / a0,
                         (1.0 - alpha) / a0});
}

void BiquadCascade::AddPeaking(double hz, double q, double gainDb)
{
    const double w = 2.0 * kPi * hz / kVoiceRate, cs = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    const double a = std::pow(10.0, gainDb / 40.0), a0 = 1.0 + alpha / a;
    sections_.push_back({(1.0 + alpha * a) / a0, -2.0 * cs / a0, (1.0 - alpha * a) / a0, -2.0 * cs / a0,
                         (1.0 - alpha / a) / a0});
}

void BiquadCascade::Process(float *interleaved, std::size_t frames)
{
    for (Section &s : sections_) {
        for (std::size_t i = 0; i < frames; ++i) {
            for (std::size_t c = 0; c < 2; ++c) {
                float &sample = interleaved[2 * i + c];
                const double x = sample, y = s.b0 * x + s.z1[c];
                s.z1[c] = s.b1 * x - s.a1 * y + s.z2[c];
                s.z2[c] = s.b2 * x - s.a2 * y;
                sample = static_cast<float>(y);
            }
        }
    }
}

/* ---- StereoStft --------------------------------------------------------------------------------------------------- */

/* The window is the square root of a Hann window, used for the analysis and again for the synthesis: their product
 * is a Hann window, which at a hop of a quarter of the frame adds up to a constant 2 (hence the 0.5 below). */
StereoStft::StereoStft() : fft_(kSize), window_(kSize), bins_(kSize)
{
    for (std::size_t n = 0; n < kSize; ++n)
        window_[n] = static_cast<float>(std::sin(kPi * static_cast<double>(n) / static_cast<double>(kSize)));
}

void StereoStft::Analyze(const float *frame)
{
    for (std::size_t n = 0; n < kSize; ++n) {
        bins_[n].re = frame[2 * n] * window_[n];
        bins_[n].im = frame[2 * n + 1] * window_[n];
    }
    fft_.Forward(bins_.data());
}

/* With z = left + i * right, bin k of the left spectrum is (Z[k] + conj(Z[-k])) / 2 and that of the right one is
 * (Z[k] - conj(Z[-k])) / 2i, so the mean of their powers is (|Z[k]|^2 + |Z[-k]|^2) / 4. */
float StereoStft::Power(std::size_t k) const
{
    const Complex &a = bins_[k], &b = bins_[(kSize - k) % kSize];
    return (a.re * a.re + a.im * a.im + b.re * b.re + b.im * b.im) * 0.25f;
}

/* A gain that is the same for k and -k is real in the time domain, so it filters both channels alike. */
void StereoStft::Apply(const float *gains)
{
    for (std::size_t k = 0; k < kBins; ++k) {
        bins_[k].re *= gains[k];
        bins_[k].im *= gains[k];
        if (k > 0 && k < kSize / 2) {
            bins_[kSize - k].re *= gains[k];
            bins_[kSize - k].im *= gains[k];
        }
    }
}

void StereoStft::Synthesize(float *overlap)
{
    fft_.Inverse(bins_.data());
    for (std::size_t n = 0; n < kSize; ++n) {
        overlap[2 * n]     += bins_[n].re * window_[n] * 0.5f;
        overlap[2 * n + 1] += bins_[n].im * window_[n] * 0.5f;
    }
}

/* ---- SpectralDenoiser --------------------------------------------------------------------------------------------- */

namespace {

constexpr float kMemory = 0.95f;            /* the weight of the previous frame in the SNR estimate */
constexpr float kTinyNoise = 1e-12f;

}  // namespace

SpectralDenoiser::SpectralDenoiser(const std::vector<float> &noise, double floorDb)
    : noise_(noise), floor_(static_cast<float>(DbToLinear(-std::fabs(floorDb)))), frame_(2 * kSize, 0.0f),
      overlap_(2 * kSize, 0.0f), pending_(2 * kHop, 0.0f), previous_(kBins, 0.0f), gains_(kBins, 1.0f),
      raw_(kBins, 1.0f), ratio_(kBins, 0.0f), skip_(kSize - kHop)
{
    noise_.resize(kBins, 0.0f);
}

void SpectralDenoiser::Process(const float *in, std::size_t frames, std::vector<float> &out)
{
    consumed_ += frames;
    while (frames > 0) {
        const std::size_t take = std::min(frames, kHop - pendingFrames_);
        std::memcpy(&pending_[2 * pendingFrames_], in, take * 2 * sizeof(float));
        pendingFrames_ += take;
        in += 2 * take;
        frames -= take;
        if (pendingFrames_ == kHop) {
            Hop(out);
            pendingFrames_ = 0;
        }
    }
}

void SpectralDenoiser::Finish(std::vector<float> &out)
{
    /* The rest of the input is followed by silence until everything that was given has come out. */
    if (pendingFrames_ > 0) {
        std::fill(pending_.begin() + 2 * pendingFrames_, pending_.end(), 0.0f);
        Hop(out);
        pendingFrames_ = 0;
    }
    std::fill(pending_.begin(), pending_.end(), 0.0f);
    while (emitted_ < consumed_)
        Hop(out);
}

/* One frame: the newest kHop frames of input arrive from `pending_`, and the oldest kHop of the overlapped output,
 * which no later frame will touch, are finished. */
void SpectralDenoiser::Hop(std::vector<float> &out)
{
    std::memmove(frame_.data(), frame_.data() + 2 * kHop, 2 * (kSize - kHop) * sizeof(float));
    std::memcpy(frame_.data() + 2 * (kSize - kHop), pending_.data(), 2 * kHop * sizeof(float));

    stft_.Analyze(frame_.data());
    for (std::size_t k = 0; k < kBins; ++k) {
        ratio_[k] = stft_.Power(k) / std::max(noise_[k], kTinyNoise);                 /* a posteriori SNR  */
        const float snr = kMemory * previous_[k] + (1.0f - kMemory) * std::max(ratio_[k] - 1.0f, 0.0f);
        raw_[k] = std::max(snr / (1.0f + snr), floor_);                               /* the Wiener gain   */
    }
    for (std::size_t k = 0; k < kBins; ++k) {       /* smoothed over frequency, the edges as they are */
        const float lo = raw_[k > 0 ? k - 1 : 0], hi = raw_[k + 1 < kBins ? k + 1 : kBins - 1];
        gains_[k] = 0.25f * lo + 0.5f * raw_[k] + 0.25f * hi;
        previous_[k] = gains_[k] * gains_[k] * ratio_[k];
    }
    stft_.Apply(gains_.data());
    stft_.Synthesize(overlap_.data());

    const std::size_t drop = std::min(skip_, kHop);
    skip_ -= drop;
    const std::size_t count = std::min(kHop - drop, consumed_ - emitted_);      /* never more than was given */
    out.insert(out.end(), overlap_.begin() + 2 * drop, overlap_.begin() + 2 * (drop + count));
    emitted_ += count;

    std::memmove(overlap_.data(), overlap_.data() + 2 * kHop, 2 * (kSize - kHop) * sizeof(float));
    std::fill(overlap_.begin() + 2 * (kSize - kHop), overlap_.end(), 0.0f);
}

/* ---- Leveler ------------------------------------------------------------------------------------------------------ */

namespace {

constexpr double kLevelWindowSeconds  = 0.010;      /* the power is averaged over this first, so a tone's ripple stays out */
constexpr double kLevelAttackSeconds  = 0.010;
constexpr double kLevelReleaseSeconds = 0.200;
constexpr double kLevelRatioShare     = 0.5;        /* half of the distance to the target is closed: 2:1  */
constexpr double kMaxBoostDb          = 9.0;
constexpr double kMaxCutDb            = 12.0;
constexpr double kGateFadeDb          = 10.0;       /* from no boost at the gate to the full boost above   */

double SmoothingCoefficient(double seconds) { return 1.0 - std::exp(-1.0 / (seconds * kVoiceRate)); }

const double kWindow  = SmoothingCoefficient(kLevelWindowSeconds);
const double kAttack  = SmoothingCoefficient(kLevelAttackSeconds);
const double kRelease = SmoothingCoefficient(kLevelReleaseSeconds);

/* The gain, in dB on top of the fixed one, for a signal `levelDb` from the target. */
double LevelerGainDb(double levelDb, double gateDb)
{
    double gain = -kLevelRatioShare * levelDb;
    if (gain > 0.0)
        gain = std::min(gain, kMaxBoostDb) * std::clamp((levelDb - gateDb) / kGateFadeDb, 0.0, 1.0);
    return std::max(gain, -kMaxCutDb);
}

}  // namespace

Leveler::Leveler(double staticGainDb, double gateDb)
    : staticDb_(staticGainDb), gateDb_(gateDb), gain_(DbToLinear(staticGainDb))
{
}

void Leveler::Process(float *interleaved, std::size_t frames)
{
    for (std::size_t i = 0; i < frames; ++i) {
        const double l = interleaved[2 * i], r = interleaved[2 * i + 1];
        power_ += kWindow * (0.5 * (l * l + r * r) - power_);
        envelope_ += (power_ > envelope_ ? kAttack : kRelease) * (power_ - envelope_);

        if (untilControl_ == 0) {       /* the wanted gain is worked out every few frames and approached in a ramp */
            const double levelDb = 10.0 * std::log10(envelope_ + 1e-12) + staticDb_ - kTargetDb;
            const double wanted = DbToLinear(staticDb_ + LevelerGainDb(levelDb, gateDb_));
            step_ = (wanted - gain_) / static_cast<double>(kControlFrames);
            untilControl_ = kControlFrames;
        }
        --untilControl_;
        gain_ += step_;
        interleaved[2 * i]     = static_cast<float>(l * gain_);
        interleaved[2 * i + 1] = static_cast<float>(r * gain_);
    }
}

/* ---- Limiter ------------------------------------------------------------------------------------------------------ */

namespace {

constexpr double kLimiterReleaseSeconds = 0.060;

}  // namespace

Limiter::Limiter(float ceiling)
    : ceiling_(ceiling), release_(SmoothingCoefficient(kLimiterReleaseSeconds)),
      audio_(2 * (kLookahead + 1), 0.0f), window_(kLookahead + 2), minima_(kLookahead + 1, 1.0f),
      sum_(static_cast<double>(kLookahead + 1)), skip_(kLookahead)
{
    /* The audio starts after kLookahead frames of silence, which make no peaks and whose output is dropped. */
    for (std::size_t i = 0; i < kLookahead; ++i)
        Push(0.0f, 0.0f, nullptr);
}

void Limiter::Process(const float *in, std::size_t frames, std::vector<float> &out)
{
    consumed_ += frames;
    for (std::size_t i = 0; i < frames; ++i)
        Push(in[2 * i], in[2 * i + 1], &out);
}

void Limiter::Finish(std::vector<float> &out)
{
    while (emitted_ < consumed_)
        Push(0.0f, 0.0f, &out);
}

/* The gain wanted at a sample is the ceiling over its peak (or 1).  The gain used for a sample is the average, over
 * the lookahead before it, of the smallest wanted gain within the lookahead after each point: so it is never above
 * what the sample itself wants, and it falls in a ramp as a peak approaches.  It rises again no faster than the
 * release allows. */
void Limiter::Push(float left, float right, std::vector<float> *out)
{
    const std::size_t slots = kLookahead + 1, now = position_;
    const float peak = std::max(std::fabs(left), std::fabs(right));
    const float wanted = peak > ceiling_ ? ceiling_ / peak : 1.0f;

    /* The sliding minimum over the last kLookahead + 1 wanted gains. */
    const std::size_t capacity = window_.size();
    while (size_ > 0 && window_[(head_ + size_ - 1) % capacity].value >= wanted)
        --size_;
    window_[(head_ + size_) % capacity] = {now, wanted};
    ++size_;
    while (window_[head_].index + kLookahead < now) {
        head_ = (head_ + 1) % capacity;
        --size_;
    }
    const float minimum = window_[head_].value;

    const float *delayed = &audio_[2 * ((now + 1) % slots)];     /* the frame from kLookahead ago */
    const float oldLeft = delayed[0], oldRight = delayed[1];
    audio_[2 * (now % slots)]     = left;
    audio_[2 * (now % slots) + 1] = right;

    sum_ += static_cast<double>(minimum) - static_cast<double>(minima_[now % slots]);
    minima_[now % slots] = minimum;
    ++position_;
    if (now < kLookahead)
        return;

    gain_ = std::min(sum_ / static_cast<double>(slots), gain_ + (1.0 - gain_) * release_);

    if (skip_ > 0) {
        --skip_;
        return;
    }
    if (out == nullptr || emitted_ >= consumed_)
        return;
    out->push_back(std::clamp(static_cast<float>(oldLeft * gain_), -ceiling_, ceiling_));
    out->push_back(std::clamp(static_cast<float>(oldRight * gain_), -ceiling_, ceiling_));
    ++emitted_;
}

}  // namespace audiorgui
