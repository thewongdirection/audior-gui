#include "speech_tap.h"

#include <cmath>

namespace audiorgui {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kCutoff = 7000.0 / 48000.0;            /* cycles per input sample */

/* A Hamming-windowed sinc, scaled to unity gain at zero frequency.  It is
 * symmetric, so it does not matter which way round the history is read. */
struct Taps {
    float value[SpeechDecimator::kTaps];

    Taps()
    {
        const int n = SpeechDecimator::kTaps;
        double sum = 0.0, raw[SpeechDecimator::kTaps];
        for (int i = 0; i < n; ++i) {
            const double x = i - (n - 1) / 2.0;
            const double sinc = x == 0.0 ? 2.0 * kCutoff : std::sin(2.0 * kPi * kCutoff * x) / (kPi * x);
            raw[i] = sinc * (0.54 - 0.46 * std::cos(2.0 * kPi * i / (n - 1)));
            sum += raw[i];
        }
        for (int i = 0; i < n; ++i)
            value[i] = static_cast<float>(raw[i] / sum);
    }
};

}  // namespace

void SpeechDecimator::Add(const std::int16_t *stereo, std::size_t frames, std::vector<std::int16_t> *out)
{
    static const Taps taps;
    for (std::size_t i = 0; i < frames; ++i) {
        const float mono = 0.5f * (static_cast<float>(stereo[2 * i]) + static_cast<float>(stereo[2 * i + 1]));
        history_[pos_] = history_[pos_ + kTaps] = mono;
        pos_ = (pos_ + 1) % kTaps;
        if (++phase_ < 3)
            continue;
        phase_ = 0;

        const float *window = history_ + pos_;          /* the oldest of the last kTaps samples first */
        float sum = 0.0f;
        for (int k = 0; k < kTaps; ++k)
            sum += taps.value[k] * window[k];
        const float clamped = sum > 32767.0f ? 32767.0f : sum < -32768.0f ? -32768.0f : sum;
        out->push_back(static_cast<std::int16_t>(std::lround(clamped)));
    }
}

}  // namespace audiorgui
