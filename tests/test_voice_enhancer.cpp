#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#include "../src/core/fft.h"
#include "../src/core/voice_dsp.h"
#include "../src/core/voice_enhancer.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

using Audio = std::vector<float>;           /* interleaved stereo */

/* The same numbers on every machine, which <random>'s distributions do not promise. */
class Random {
public:
    explicit Random(std::uint32_t seed) : state_(seed) {}
    float Next()                            /* -1 to +1 */
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(state_)) / 2147483648.0f;
    }

private:
    std::uint32_t state_;
};

double AmplitudeOfDb(double db) { return std::pow(10.0, db / 20.0); }

Audio Silence(std::size_t frames) { return Audio(2 * frames, 0.0f); }

/* The same tone on both channels. */
Audio Tone(std::size_t frames, double hz, double amplitude)
{
    Audio a(2 * frames);
    for (std::size_t i = 0; i < frames; ++i)
        a[2 * i] = a[2 * i + 1] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate));
    return a;
}

/* Uniform noise whose RMS is `rmsDb` dBFS, the same on both channels unless `separate`. */
Audio Noise(std::size_t frames, double rmsDb, std::uint32_t seed, bool separate = false)
{
    Random random(seed), other(seed + 1);
    const float scale = static_cast<float>(AmplitudeOfDb(rmsDb) * std::sqrt(3.0));
    Audio a(2 * frames);
    for (std::size_t i = 0; i < frames; ++i) {
        a[2 * i] = scale * random.Next();
        a[2 * i + 1] = separate ? scale * other.Next() : a[2 * i];
    }
    return a;
}

void Add(Audio &into, const Audio &other)
{
    for (std::size_t i = 0; i < into.size() && i < other.size(); ++i)
        into[i] += other[i];
}

double RmsDb(const Audio &a, std::size_t fromFrame, std::size_t toFrame)
{
    double sum = 0.0;
    for (std::size_t i = 2 * fromFrame; i < 2 * toFrame; ++i)
        sum += static_cast<double>(a[i]) * a[i];
    return 10.0 * std::log10(sum / static_cast<double>(2 * (toFrame - fromFrame)) + 1e-20);
}

double PeakOf(const Audio &a)
{
    float peak = 0.0f;
    for (float s : a)
        peak = std::max(peak, std::fabs(s));
    return peak;
}

/* A voice-like sound: harmonics of 130 Hz with a body at 500 Hz and 1.5 kHz, in syllables of 4 a second.  Its RMS
 * over the syllables is about `rmsDb`. */
Audio Speech(std::size_t frames, double rmsDb)
{
    double body[31];
    for (int h = 1; h <= 30; ++h) {
        const double f = 130.0 * h;
        body[h] = 1.0 / h + 2.0 / (1.0 + std::pow((f - 500.0) / 150.0, 2.0)) +
                  1.0 / (1.0 + std::pow((f - 1500.0) / 300.0, 2.0));
    }
    Audio a(2 * frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kRate;
        double voice = 0.0;
        for (int h = 1; h <= 30; ++h)
            voice += body[h] * std::sin(2.0 * kPi * 130.0 * h * t);
        const double syllable = 0.5 * (1.0 - std::cos(2.0 * kPi * 4.0 * t)) + 0.15;
        a[2 * i] = a[2 * i + 1] = static_cast<float>(voice * syllable);
    }
    const double now = RmsDb(a, 0, frames);
    const float scale = static_cast<float>(AmplitudeOfDb(rmsDb - now));
    for (float &s : a)
        s *= scale;
    return a;
}

/* Three stretches of speech with pauses between and a noise floor under everything. */
struct Lecture {
    Audio       audio;
    std::size_t speechFrom[3], speechTo[3];
};

Lecture MakeLecture(double speechDb, double noiseDb)
{
    const std::size_t second = 48000;
    Lecture lecture;
    lecture.audio = Noise(6 * second, noiseDb, 7, true);
    const std::size_t from[3] = {second / 2, 2 * second + second / 2, 4 * second + second / 2};
    const Audio voice = Speech(3 * second / 2, speechDb);
    for (int i = 0; i < 3; ++i) {
        lecture.speechFrom[i] = from[i];
        lecture.speechTo[i] = from[i] + 3 * second / 2;
        for (std::size_t j = 0; j < voice.size(); ++j)
            lecture.audio[2 * from[i] + j] += voice[j];
    }
    return lecture;
}

double SpeechDb(const Lecture &l, const Audio &a)
{
    double sum = 0.0;
    std::size_t count = 0;
    for (int i = 0; i < 3; ++i)
        for (std::size_t j = 2 * l.speechFrom[i]; j < 2 * l.speechTo[i]; ++j, ++count)
            sum += static_cast<double>(a[j]) * a[j];
    return 10.0 * std::log10(sum / static_cast<double>(count));
}

double PauseDb(const Lecture &l, const Audio &a)         /* the pause between the first two stretches, less 0.1 s at each end */
{
    return RmsDb(a, l.speechTo[0] + 4800, l.speechFrom[1] - 4800);
}

VoiceProfile Analyze(const Audio &in, std::size_t chunk)
{
    VoiceAnalyzer analyzer;
    for (std::size_t at = 0; at < in.size() / 2; at += chunk)
        analyzer.Add(&in[2 * at], std::min(chunk, in.size() / 2 - at));
    return analyzer.Finish();
}

Audio Enhance(const Audio &in, const VoiceProfile &profile, std::size_t chunk)
{
    VoiceEnhancer enhancer(profile);
    Audio out;
    for (std::size_t at = 0; at < in.size() / 2; at += chunk)
        enhancer.Process(&in[2 * at], std::min(chunk, in.size() / 2 - at), out);
    enhancer.Finish(out);
    return out;
}

Audio Enhance(const Audio &in) { return Enhance(in, Analyze(in, 4800), 4800); }

/* The gain of a filter at one frequency, from the end of a long tone. */
double GainDb(BiquadCascade &filter, double hz)
{
    const std::size_t frames = 24000;
    Audio a = Tone(frames, hz, 0.1);
    filter.Process(a.data(), frames);
    return RmsDb(a, frames / 2, frames) - RmsDb(Tone(frames, hz, 0.1), frames / 2, frames);
}

}  // namespace

/* ---- Fft ---------------------------------------------------------------------------------------------------------- */

TEST(Fft_MatchesTheDefinition)
{
    const std::size_t n = 16;
    Random random(1);
    std::vector<Complex> data(n);
    for (Complex &c : data) {
        c.re = random.Next();
        c.im = random.Next();
    }
    std::vector<Complex> fast = data;
    Fft(n).Forward(fast.data());
    for (std::size_t k = 0; k < n; ++k) {
        std::complex<double> sum = 0.0;
        for (std::size_t j = 0; j < n; ++j)
            sum += std::complex<double>(data[j].re, data[j].im) *
                   std::polar(1.0, -2.0 * kPi * static_cast<double>(j * k) / static_cast<double>(n));
        CHECK_NEAR(fast[k].re, sum.real(), 1e-5);
        CHECK_NEAR(fast[k].im, sum.imag(), 1e-5);
    }
}

TEST(Fft_InverseGivesTheInputBack)
{
    Random random(2);
    std::vector<Complex> data(1024), copy;
    for (Complex &c : data) {
        c.re = random.Next();
        c.im = random.Next();
    }
    copy = data;
    const Fft fft(1024);
    fft.Forward(copy.data());
    fft.Inverse(copy.data());
    for (std::size_t i = 0; i < data.size(); ++i) {
        CHECK_NEAR(copy[i].re, data[i].re, 1e-5);
        CHECK_NEAR(copy[i].im, data[i].im, 1e-5);
    }
}

TEST(Fft_ASineIsOnePeak)
{
    std::vector<Complex> data(64);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i].re = static_cast<float>(std::sin(2.0 * kPi * 5.0 * static_cast<double>(i) / 64.0));
    Fft(64).Forward(data.data());
    CHECK_NEAR(std::hypot(data[5].re, data[5].im), 32.0, 1e-3);
    CHECK_NEAR(std::hypot(data[7].re, data[7].im), 0.0, 1e-3);
}

/* ---- the filters -------------------------------------------------------------------------------------------------- */

TEST(Biquad_HighPassKeepsTheVoiceAndCutsTheRumble)
{
    BiquadCascade high, low, lower;
    high.AddHighPass(80.0, 0.7071);
    low.AddHighPass(80.0, 0.7071);
    lower.AddHighPass(80.0, 0.7071);
    CHECK_NEAR(GainDb(high, 1000.0), 0.0, 0.1);
    CHECK_NEAR(GainDb(low, 80.0), -3.0, 0.4);           /* the corner */
    CHECK(GainDb(lower, 20.0) < -20.0);
}

TEST(Biquad_PeakingBoostsOnlyAroundItsFrequency)
{
    BiquadCascade a, b, c;
    for (BiquadCascade *f : {&a, &b, &c})
        f->AddPeaking(3000.0, 0.9, 2.5);
    CHECK_NEAR(GainDb(a, 3000.0), 2.5, 0.1);
    CHECK_NEAR(GainDb(b, 200.0), 0.0, 0.3);
    CHECK_NEAR(GainDb(c, 15000.0), 0.0, 0.5);
}

TEST(Biquad_ChannelsDoNotMix)
{
    BiquadCascade filter;
    filter.AddPeaking(1000.0, 1.0, 6.0);
    Audio a = Tone(4800, 1000.0, 0.2);
    for (std::size_t i = 0; i < 4800; ++i)
        a[2 * i + 1] = 0.0f;
    filter.Process(a.data(), 4800);
    for (std::size_t i = 0; i < 4800; ++i)
        CHECK(a[2 * i + 1] == 0.0f);
    CHECK(PeakOf(a) > 0.2);
}

/* ---- the spectrum ------------------------------------------------------------------------------------------------- */

TEST(StereoStft_AnalysisAndSynthesisGiveTheSignalBack)
{
    const std::size_t total = 8 * StereoStft::kSize;
    Audio in(2 * total);
    Random random(3);
    for (float &s : in)
        s = random.Next();

    StereoStft stft;
    Audio sum(2 * total, 0.0f);
    for (std::size_t at = 0; at + StereoStft::kSize <= total; at += StereoStft::kHop) {
        stft.Analyze(&in[2 * at]);
        stft.Synthesize(&sum[2 * at]);
    }
    /* Between the first and the last full overlap every sample has been covered four times. */
    for (std::size_t i = 2 * StereoStft::kSize; i < 2 * (total - StereoStft::kSize); ++i)
        CHECK_NEAR(sum[i], in[i], 1e-4);
}

TEST(StereoStft_PowerIsTheMeanOfTheTwoChannels)
{
    const std::size_t n = StereoStft::kSize;
    Audio left = Silence(n), right = Silence(n), both(2 * n);
    const Audio tone = Tone(n, 20.0 * kRate / n, 0.5);          /* exactly on bin 20 */
    for (std::size_t i = 0; i < n; ++i) {
        left[2 * i] = tone[2 * i];
        right[2 * i + 1] = tone[2 * i];
        both[2 * i] = both[2 * i + 1] = tone[2 * i];
    }
    StereoStft stft;
    stft.Analyze(left.data());
    const float l = stft.Power(20);
    stft.Analyze(right.data());
    const float r = stft.Power(20);
    stft.Analyze(both.data());
    const float b = stft.Power(20);
    CHECK(l > 1.0f);
    CHECK_NEAR(r, l, l * 1e-4);
    CHECK_NEAR(b, 2.0f * l, l * 1e-3);
    CHECK(stft.Power(100) < l * 1e-4f);
}

/* ---- the denoiser ------------------------------------------------------------------------------------------------- */

namespace {

Audio Denoise(const Audio &in, const std::vector<float> &noise, std::size_t chunk)
{
    SpectralDenoiser denoiser(noise, 12.0);
    Audio out;
    for (std::size_t at = 0; at < in.size() / 2; at += chunk)
        denoiser.Process(&in[2 * at], std::min(chunk, in.size() / 2 - at), out);
    denoiser.Finish(out);
    return out;
}

}  // namespace

TEST(SpectralDenoiser_WithNoNoiseToRemoveItChangesNothing)
{
    const Audio in = Noise(20000, -20.0, 4, true);
    const Audio out = Denoise(in, std::vector<float>(StereoStft::kBins, 0.0f), 1000);
    CHECK_EQ(out.size(), in.size());
    for (std::size_t i = 0; i < in.size() && i < out.size(); ++i)
        CHECK_NEAR(out[i], in[i], 1e-4);
}

TEST(SpectralDenoiser_GivesBackAsMuchAsItWasGivenWhateverThePieces)
{
    const std::vector<float> noise(StereoStft::kBins, 1e-6f);
    for (std::size_t frames : {std::size_t(0), std::size_t(1), std::size_t(255), std::size_t(256), std::size_t(257),
                               std::size_t(767), std::size_t(768), std::size_t(769), std::size_t(1023), std::size_t(1024),
                               std::size_t(1025), std::size_t(5000)}) {
        const Audio in = Noise(frames, -30.0, 5);
        for (std::size_t chunk : {std::size_t(1), std::size_t(300), std::size_t(4096)})
            CHECK_EQ(Denoise(in, noise, chunk).size(), in.size());
    }
}

TEST(SpectralDenoiser_TheSizeOfThePiecesMakesNoDifference)
{
    const Audio in = Noise(9000, -30.0, 6, true);
    const std::vector<float> noise(StereoStft::kBins, 1e-5f);
    const Audio whole = Denoise(in, noise, 9000);
    CHECK(Denoise(in, noise, 1) == whole);
    CHECK(Denoise(in, noise, 777) == whole);
}

TEST(SpectralDenoiser_TakesSteadyNoiseDownAndKeepsATone)
{
    const std::size_t frames = 48000;
    const Audio noise = Noise(frames, -40.0, 8, true);
    Audio mixed = noise;
    const Audio tone = Tone(frames, 1000.0, 0.1);              /* -23 dBFS */
    Add(mixed, tone);

    /* The power per bin of white noise at -40 dBFS in a window of the frame's size. */
    const std::vector<float> psd(StereoStft::kBins, static_cast<float>(AmplitudeOfDb(-80.0) * StereoStft::kSize / 2));
    const Audio onlyNoise = Denoise(noise, psd, 4800);
    CHECK(RmsDb(onlyNoise, 4800, frames - 4800) < RmsDb(noise, 4800, frames - 4800) - 9.0);

    const Audio out = Denoise(mixed, psd, 4800);
    CHECK_NEAR(RmsDb(out, 9600, frames - 9600), RmsDb(tone, 9600, frames - 9600), 1.0);
}

TEST(SpectralDenoiser_NeverCutsMoreThanItsFloor)
{
    const Audio noise = Noise(48000, -40.0, 14, true);
    /* A noise estimate a hundred times too high asks for the deepest cut everywhere. */
    const std::vector<float> psd(StereoStft::kBins, static_cast<float>(AmplitudeOfDb(-80.0) * StereoStft::kSize / 2 * 100.0));
    const Audio out = Denoise(noise, psd, 4800);
    const double cut = RmsDb(noise, 9600, 38400) - RmsDb(out, 9600, 38400);
    CHECK(cut > 10.0 && cut < 12.5);                        /* the floor is 12 dB */
}

/* ---- the leveler -------------------------------------------------------------------------------------------------- */

TEST(Leveler_SpeechAtTheTargetIsLeftAlone)
{
    Audio a = Tone(48000, 500.0, AmplitudeOfDb(Leveler::kTargetDb) * std::sqrt(2.0));
    const Audio before = a;
    Leveler leveler(0.0, -40.0);
    leveler.Process(a.data(), 48000);
    CHECK_NEAR(RmsDb(a, 24000, 48000), RmsDb(before, 24000, 48000), 0.3);
}

TEST(Leveler_TheFixedGainIsApplied)
{
    Audio a = Tone(48000, 500.0, AmplitudeOfDb(Leveler::kTargetDb - 10.0) * std::sqrt(2.0));
    Leveler leveler(10.0, -40.0);
    leveler.Process(a.data(), 48000);
    CHECK_NEAR(RmsDb(a, 24000, 48000), Leveler::kTargetDb, 0.3);
}

TEST(Leveler_PullsLoudAndSoftTowardTheTargetByHalf)
{
    Audio loud = Tone(48000, 500.0, AmplitudeOfDb(Leveler::kTargetDb + 8.0) * std::sqrt(2.0));
    Audio soft = Tone(48000, 500.0, AmplitudeOfDb(Leveler::kTargetDb - 8.0) * std::sqrt(2.0));
    Leveler a(0.0, -40.0), b(0.0, -40.0);
    a.Process(loud.data(), 48000);
    b.Process(soft.data(), 48000);
    CHECK_NEAR(RmsDb(loud, 24000, 48000), Leveler::kTargetDb + 4.0, 0.5);
    CHECK_NEAR(RmsDb(soft, 24000, 48000), Leveler::kTargetDb - 4.0, 0.5);
}

TEST(Leveler_DoesNotRaiseWhatIsUnderTheGate)
{
    Audio a = Tone(48000, 500.0, AmplitudeOfDb(Leveler::kTargetDb - 30.0) * std::sqrt(2.0));
    const Audio before = a;
    Leveler leveler(0.0, -25.0);
    leveler.Process(a.data(), 48000);
    CHECK_NEAR(RmsDb(a, 24000, 48000), RmsDb(before, 24000, 48000), 0.3);
}

TEST(Leveler_AnyPieceSizeGivesTheSameResult)
{
    Audio whole = Noise(10000, -25.0, 9, true), pieces = whole;
    Leveler a(6.0, -30.0), b(6.0, -30.0);
    a.Process(whole.data(), 10000);
    for (std::size_t at = 0; at < 10000; at += 97)
        b.Process(&pieces[2 * at], std::min<std::size_t>(97, 10000 - at));
    CHECK(whole == pieces);
}

/* ---- the limiter -------------------------------------------------------------------------------------------------- */

namespace {

Audio Limit(const Audio &in, float ceiling, std::size_t chunk)
{
    Limiter limiter(ceiling);
    Audio out;
    for (std::size_t at = 0; at < in.size() / 2; at += chunk)
        limiter.Process(&in[2 * at], std::min(chunk, in.size() / 2 - at), out);
    limiter.Finish(out);
    return out;
}

}  // namespace

TEST(Limiter_LeavesWhatIsUnderTheCeilingExactlyAsItIs)
{
    const Audio in = Noise(5000, -20.0, 10, true);
    CHECK(Limit(in, 0.9f, 333) == in);
}

TEST(Limiter_NothingPassesTheCeilingAndTheLengthIsKept)
{
    Audio in = Noise(30000, -6.0, 11, true);
    for (std::size_t i = 0; i < 20; ++i)                /* a few much louder bursts */
        in[2 * (1500 * i + 100)] = (i % 2 ? 1.0f : -1.0f) * 4.0f;
    for (std::size_t chunk : {std::size_t(1), std::size_t(128), std::size_t(30000)}) {
        const Audio out = Limit(in, 0.8f, chunk);
        CHECK_EQ(out.size(), in.size());
        CHECK(PeakOf(out) <= 0.8f);
    }
}

TEST(Limiter_AShortInputComesOutWhole)
{
    for (std::size_t frames : {std::size_t(1), std::size_t(50), std::size_t(96), std::size_t(97), std::size_t(500)}) {
        const Audio in = Noise(frames, -3.0, 12);
        const Audio out = Limit(in, 0.5f, 7);
        CHECK_EQ(out.size(), in.size());
        CHECK(PeakOf(out) <= 0.5f);
    }
}

TEST(Limiter_TheGainComesDownBeforeAPeakAndBackUpAfterIt)
{
    Audio in = Tone(48000, 300.0, 0.4);
    const std::size_t peakAt = 24000;
    in[2 * peakAt] = in[2 * peakAt + 1] = 2.0f;                 /* a click, 14 dB over the ceiling of 0.4 */
    const Audio out = Limit(in, 0.4f, 1000);
    CHECK(PeakOf(out) <= 0.4f);
    /* Well before and a few tenths of a second after, the tone is as it was. */
    CHECK_NEAR(RmsDb(out, 20000, 22000), RmsDb(in, 20000, 22000), 0.05);
    CHECK_NEAR(RmsDb(out, 45000, 48000), RmsDb(in, 45000, 48000), 0.02);
    /* Next to the click the gain was pulled down. */
    CHECK(RmsDb(out, peakAt - 40, peakAt + 40) < RmsDb(in, peakAt - 40, peakAt - 1) - 3.0);
}

/* ---- the analysis ------------------------------------------------------------------------------------------------- */

TEST(VoiceAnalyzer_SilenceHasNothingToRemoveAndNothingToRaise)
{
    const VoiceProfile profile = Analyze(Silence(20000), 1000);
    CHECK_EQ(profile.gainDb, 0.0);
    for (float n : profile.noise)
        CHECK(n == 0.0f);
}

TEST(VoiceAnalyzer_FindsTheSpeechLevelAndTheNoiseLevel)
{
    const Lecture quiet = MakeLecture(-34.0, -62.0);
    const VoiceProfile profile = Analyze(quiet.audio, 4800);
    /* Raised to about the target; the exact figure depends on the syllables, so the margin is wide. */
    CHECK(profile.gainDb > 8.0 && profile.gainDb < 20.0);
    float strongest = 0.0f;
    for (float n : profile.noise)
        strongest = std::max(strongest, n);
    CHECK(strongest > 0.0f);

    const Lecture loud = MakeLecture(-8.0, -62.0);
    CHECK(Analyze(loud.audio, 4800).gainDb < -5.0);
}

TEST(VoiceAnalyzer_ThePieceSizeMakesNoDifference)
{
    const Lecture l = MakeLecture(-30.0, -55.0);
    const VoiceProfile a = Analyze(l.audio, 48000), b = Analyze(l.audio, 333);
    CHECK_EQ(a.gainDb, b.gainDb);
    CHECK_EQ(a.gateDb, b.gateDb);
    CHECK(a.noise == b.noise);
}

TEST(VoiceAnalyzer_AVeryShortRecordingIsStillLookedAt)
{
    const VoiceProfile profile = Analyze(Tone(300, 440.0, 0.01), 100);
    CHECK(profile.gainDb > 0.0);
}

/* ---- the whole chain ---------------------------------------------------------------------------------------------- */

TEST(VoiceEnhancer_SilenceStaysSilenceAndTheLengthIsKept)
{
    const Audio out = Enhance(Silence(30001));
    CHECK_EQ(out.size(), std::size_t(2 * 30001));
    CHECK(PeakOf(out) == 0.0);
}

TEST(VoiceEnhancer_GivesBackAsMuchAsItWasGivenWhateverThePieces)
{
    const Audio in = Noise(7000, -40.0, 13, true);
    const VoiceProfile profile = Analyze(in, 1000);
    for (std::size_t frames : {std::size_t(0), std::size_t(1), std::size_t(95), std::size_t(1000), std::size_t(7000)}) {
        const Audio part(in.begin(), in.begin() + 2 * frames);
        for (std::size_t chunk : {std::size_t(1), std::size_t(500), std::size_t(8192)})
            CHECK_EQ(Enhance(part, profile, chunk).size(), part.size());
    }
}

TEST(VoiceEnhancer_TheSizeOfThePiecesMakesNoDifference)
{
    const Lecture l = MakeLecture(-30.0, -55.0);
    const Audio in(l.audio.begin(), l.audio.begin() + 2 * 40000);
    const VoiceProfile profile = Analyze(in, 4800);
    const Audio whole = Enhance(in, profile, 40000);
    CHECK(Enhance(in, profile, 1234) == whole);
    CHECK(Enhance(in, profile, 1) == whole);
}

TEST(VoiceEnhancer_IsTheSameEveryTime)
{
    const Lecture l = MakeLecture(-30.0, -55.0);
    const Audio part(l.audio.begin(), l.audio.begin() + 2 * 48000);
    CHECK(Enhance(part) == Enhance(part));
}

TEST(VoiceEnhancer_QuietAndLoudSpeechComeOutAtTheSameLevel)
{
    const Lecture quiet = MakeLecture(-40.0, -64.0), loud = MakeLecture(-18.0, -42.0);
    const Audio fromQuiet = Enhance(quiet.audio), fromLoud = Enhance(loud.audio);
    CHECK_EQ(fromQuiet.size(), quiet.audio.size());

    /* The speech over its syllables, pauses included, is a few dB under the level of the loud parts. */
    const double a = SpeechDb(quiet, fromQuiet), b = SpeechDb(loud, fromLoud);
    CHECK_NEAR(a, b, 1.5);
    CHECK(a > Leveler::kTargetDb - 8.0 && a < Leveler::kTargetDb);
}

TEST(VoiceEnhancer_TheNoiseBetweenWordsFallsAwayFromTheSpeech)
{
    const Lecture l = MakeLecture(-34.0, -58.0);
    const Audio out = Enhance(l.audio);
    const double before = SpeechDb(l, l.audio) - PauseDb(l, l.audio), after = SpeechDb(l, out) - PauseDb(l, out);
    CHECK(after > before + 6.0);
}

TEST(VoiceEnhancer_LoudSpeechIsBroughtDownAndNeverClips)
{
    const Lecture l = MakeLecture(-6.0, -60.0);
    Audio loud = l.audio;
    for (float &s : loud)
        s = std::clamp(s * 3.0f, -1.0f, 1.0f);              /* recorded too hot */
    const Audio out = Enhance(loud);
    CHECK(PeakOf(out) <= AmplitudeOfDb(-1.0) + 1e-6);
    CHECK(SpeechDb(l, out) < SpeechDb(l, loud) - 3.0);
}

TEST(VoiceEnhancer_CleanSpeechIsNotDamaged)
{
    /* No noise to speak of: the voice is raised to the target and the pauses are not filled with anything. */
    const Lecture l = MakeLecture(-26.0, -84.0);
    const Audio out = Enhance(l.audio);
    const double speech = SpeechDb(l, out);
    CHECK(speech > Leveler::kTargetDb - 8.0 && speech < Leveler::kTargetDb);
    CHECK(PauseDb(l, out) < -80.0);                     /* and the quiet stays quiet */
}

TEST(VoiceEnhancer_KeepsTheChannelsApart)
{
    /* Mono in both channels stays the same in both; a voice on one side leaves the other side empty. */
    Lecture l = MakeLecture(-30.0, -60.0);
    for (std::size_t i = 0; i < l.audio.size() / 2; ++i)
        l.audio[2 * i + 1] = l.audio[2 * i];
    const Audio both = Enhance(l.audio);
    for (std::size_t i = 0; i < both.size() / 2; ++i)
        CHECK_NEAR(both[2 * i], both[2 * i + 1], 1e-5);

    Audio left = Speech(48000, -30.0);
    for (std::size_t i = 0; i < 48000; ++i)
        left[2 * i + 1] = 0.0f;
    const Audio out = Enhance(left);
    double rightPeak = 0.0;
    for (std::size_t i = 0; i < out.size() / 2; ++i)
        rightPeak = std::max(rightPeak, static_cast<double>(std::fabs(out[2 * i + 1])));
    CHECK(rightPeak < 1e-4);
}

TEST(VoiceEnhancer_RumbleDoesNotThrowOffTheLevelOfTheSpeech)
{
    /* The level is judged on what is left once the rumble is gone: the same speech comes out as loud with or without it. */
    const Audio speech = Speech(96000, -30.0);
    Audio withHum = speech;
    Add(withHum, Tone(96000, 30.0, 0.05));
    const Audio clean = Enhance(speech), humming = Enhance(withHum);
    CHECK_NEAR(RmsDb(humming, 24000, 96000), RmsDb(clean, 24000, 96000), 1.0);
}

TEST(VoiceEnhancer_TakesTheRumbleOut)
{
    Audio in = Tone(96000, 30.0, 0.05);                 /* a 30 Hz hum as loud as the speech */
    Add(in, Speech(96000, -30.0));
    const Audio out = Enhance(in);
    CHECK_EQ(out.size(), in.size());

    /* How strong the hum is, against the whole sound, before and after. */
    const auto humDb = [](const Audio &a) {
        double re = 0.0, im = 0.0;
        const std::size_t from = 24000, to = 96000;     /* 45 whole cycles */
        for (std::size_t i = from; i < to; ++i) {
            const double w = 2.0 * kPi * 30.0 * static_cast<double>(i) / kRate;
            re += a[2 * i] * std::cos(w);
            im += a[2 * i] * std::sin(w);
        }
        return 20.0 * std::log10(2.0 * std::hypot(re, im) / static_cast<double>(to - from)) - RmsDb(a, from, to);
    };
    CHECK(humDb(out) < humDb(in) - 25.0);
}
