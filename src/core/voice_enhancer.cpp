#include "voice_enhancer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace audiorgui {

namespace {

constexpr std::size_t kSize = StereoStft::kSize;
constexpr std::size_t kBins = StereoStft::kBins;

/* The analysis. */
constexpr std::size_t kAnalysisHop   = kSize / 2;
constexpr double      kSilentPower   = 3.2e-9;      /* -85 dBFS: under this a frame is silence, not noise        */
constexpr double      kNoiseBias     = 1.3;         /* the quietest frames are quieter than the noise on average  */
constexpr std::size_t kNoiseSmoothing = 4;          /* bins each side: the noise shape is smooth, its estimate not */
constexpr double      kSpeechSpanDb  = 20.0;        /* the loud part: within this of the 95th percentile          */
constexpr double      kMinGainDb     = -10.0;
constexpr double      kMaxGainDb     = 25.0;
constexpr double      kGateMarginDb  = 8.0;         /* the soft parts are raised only this far above the noise    */
constexpr double      kMinGateDb     = -40.0;
constexpr double      kMaxGateDb     = -15.0;
constexpr double      kClearGapDb    = 20.0;        /* speech over noise: from here on the full denoising          */
constexpr double      kNoGapDb       = 6.0;         /* ... and none at all at this gap (the "noise" is speech)     */

/* The processing. */
constexpr double kHighPassHz    = 80.0;         /* a 4th-order Butterworth: two sections of these Qs */
constexpr double kHighPassQ[2]  = {0.5412, 1.3066};
constexpr double kMudHz         = 250.0, kMudGainDb = -2.0, kMudQ = 1.0;
constexpr double kPresenceHz    = 3000.0, kPresenceGainDb = 2.5, kPresenceQ = 0.9;
constexpr double kDenoiseFloorDb = 12.0;
constexpr double kCeilingDb     = -1.0;

double Db(double power) { return 10.0 * std::log10(power); }

/* The first thing the chain does, and the first thing the analysis does, so that both go by the same signal. */
void AddRumbleFilter(BiquadCascade *filter)
{
    for (double q : kHighPassQ)
        filter->AddHighPass(kHighPassHz, q);
}

}  // namespace

/* ---- VoiceAnalyzer ------------------------------------------------------------------------------------------------ */

VoiceAnalyzer::VoiceAnalyzer()
    : frame_(2 * kSize, 0.0f), spectra_(kQuietFrames * kBins), levelCount_(kLevelBins, 0.0),
      levelPower_(kLevelBins, 0.0)
{
    quiet_.reserve(kQuietFrames);
    AddRumbleFilter(&rumble_);
}

void VoiceAnalyzer::Add(const float *interleaved, std::size_t frames)
{
    while (frames > 0) {
        const std::size_t take = std::min(frames, kSize - fill_);
        std::memcpy(&frame_[2 * fill_], interleaved, take * 2 * sizeof(float));
        rumble_.Process(&frame_[2 * fill_], take);          /* the signal as the chain will have it */
        fill_ += take;
        interleaved += 2 * take;
        frames -= take;
        if (fill_ == kSize) {
            AnalyzeFrame();
            std::memmove(frame_.data(), frame_.data() + 2 * kAnalysisHop, 2 * (kSize - kAnalysisHop) * sizeof(float));
            fill_ = kSize - kAnalysisHop;
        }
    }
}

/* Every frame can be one of the quietest (silence among them: a recording that has some has no noise to take out);
 * only one with sound in it counts for the level of the speech.  A frame is transformed only if it joins the quietest. */
void VoiceAnalyzer::AnalyzeFrame()
{
    ++frames_;
    double sum = 0.0;
    for (float sample : frame_)
        sum += static_cast<double>(sample) * sample;
    const double power = sum / static_cast<double>(frame_.size());
    if (power >= kSilentPower) {
        ++analyzed_;
        const std::size_t level = static_cast<std::size_t>(std::clamp(std::floor(Db(power)) + 100.0, 0.0, 100.0));
        levelCount_[level] += 1.0;
        levelPower_[level] += power;
    }

    const auto louder = [](const Quiet &a, const Quiet &b) { return a.power < b.power; };
    std::size_t slot = quiet_.size();
    if (slot == kQuietFrames) {
        if (power >= quiet_.front().power)
            return;
        std::pop_heap(quiet_.begin(), quiet_.end(), louder);        /* the loudest goes to the back; its place is ours */
        slot = quiet_.back().slot;
        quiet_.back() = {power, slot};
    } else {
        quiet_.push_back({power, slot});
    }
    stft_.Analyze(frame_.data());
    for (std::size_t k = 0; k < kBins; ++k)
        spectra_[slot * kBins + k] = stft_.Power(k);
    std::push_heap(quiet_.begin(), quiet_.end(), louder);
}

VoiceProfile VoiceAnalyzer::Finish()
{
    if (frames_ == 0 && fill_ > 0) {                    /* shorter than one frame: look at it as it is */
        std::fill(frame_.begin() + 2 * fill_, frame_.end(), 0.0f);
        AnalyzeFrame();
    }
    VoiceProfile profile;
    profile.noise.assign(kBins, 0.0f);
    if (analyzed_ == 0)
        return profile;                                 /* nothing but silence: nothing to level or to clean */

    /* The speech: the frames within a span of the 95th percentile, by their mean power. */
    const double fivePercent = std::max(1.0, 0.05 * static_cast<double>(analyzed_));
    std::size_t top = kLevelBins - 1;
    double seen = levelCount_[top];
    while (seen < fivePercent && top > 0)
        seen += levelCount_[--top];
    double count = 0.0, power = 0.0;
    const std::size_t span = static_cast<std::size_t>(kSpeechSpanDb);        /* a level bin is 1 dB */
    for (std::size_t b = top > span ? top - span : 0; b < kLevelBins; ++b) {
        count += levelCount_[b];
        power += levelPower_[b];
    }
    const double speechDb = Db(power / count);
    profile.gainDb = std::clamp(Leveler::kTargetDb - speechDb, kMinGainDb, kMaxGainDb);

    /* The noise: the average spectrum of the quietest frames (an eighth of all, at least four, at most kQuietFrames). */
    std::sort_heap(quiet_.begin(), quiet_.end(), [](const Quiet &a, const Quiet &b) { return a.power < b.power; });
    const std::size_t use = std::min(quiet_.size(), std::max<std::size_t>(4, frames_ / 8));
    std::vector<double> mean(kBins, 0.0);
    double noisePower = 0.0;
    for (std::size_t i = 0; i < use; ++i) {
        noisePower += quiet_[i].power / static_cast<double>(use);
        for (std::size_t k = 0; k < kBins; ++k)
            mean[k] += spectra_[quiet_[i].slot * kBins + k] / static_cast<double>(use);
    }
    const double noiseDb = Db(std::max(noisePower, 1e-12));

    /* The closer the "noise" is to the speech, the likelier it is not noise at all: then it is taken out less. */
    const double clear = std::clamp((speechDb - noiseDb - kNoGapDb) / (kClearGapDb - kNoGapDb), 0.0, 1.0);
    for (std::size_t k = 0; k < kBins; ++k) {
        const std::size_t from = k > kNoiseSmoothing ? k - kNoiseSmoothing : 0, to = std::min(k + kNoiseSmoothing, kBins - 1);
        double sum = 0.0;
        for (std::size_t j = from; j <= to; ++j)
            sum += mean[j];
        profile.noise[k] = static_cast<float>(sum / static_cast<double>(to - from + 1) * kNoiseBias * clear);
    }

    const double noiseFromTarget = noiseDb + profile.gainDb - Leveler::kTargetDb;
    profile.gateDb = std::clamp(noiseFromTarget + kGateMarginDb, kMinGateDb, kMaxGateDb);
    return profile;
}

/* ---- VoiceEnhancer ------------------------------------------------------------------------------------------------ */

VoiceEnhancer::VoiceEnhancer(const VoiceProfile &profile)
    : denoiser_(profile.noise, kDenoiseFloorDb), leveler_(profile.gainDb, profile.gateDb),
      limiter_(static_cast<float>(std::pow(10.0, kCeilingDb / 20.0)))
{
    AddRumbleFilter(&before_);
    after_.AddPeaking(kMudHz, kMudQ, kMudGainDb);
    after_.AddPeaking(kPresenceHz, kPresenceQ, kPresenceGainDb);
}

void VoiceEnhancer::Process(const float *in, std::size_t frames, std::vector<float> &out)
{
    input_.assign(in, in + 2 * frames);
    before_.Process(input_.data(), frames);
    denoised_.clear();
    denoiser_.Process(input_.data(), frames, denoised_);
    Tail(out);
}

void VoiceEnhancer::Finish(std::vector<float> &out)
{
    denoised_.clear();
    denoiser_.Finish(denoised_);
    Tail(out);
    limiter_.Finish(out);
}

void VoiceEnhancer::Tail(std::vector<float> &out)
{
    const std::size_t frames = denoised_.size() / 2;
    after_.Process(denoised_.data(), frames);
    leveler_.Process(denoised_.data(), frames);
    limiter_.Process(denoised_.data(), frames, out);
}

}  // namespace audiorgui
