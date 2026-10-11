#include "simulated_engine.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace audiorgui {

namespace {

constexpr double kPi = 3.14159265358979323846;

/* Rates for sizing the pretend output, as audior's writers produce. */
constexpr double kWavBytesPerSecond = 48000.0 * 2 * 2;      /* 48 kHz, 16-bit, stereo */

}  // namespace

SimulatedEngine::SimulatedEngine(Clock nowSeconds, std::uint64_t seed)
    : now_(std::move(nowSeconds)), seed_(seed)
{
}

void SimulatedEngine::FailAfter(double seconds, std::wstring message)
{
    failAfter_ = seconds;
    failMessage_ = std::move(message);
}

std::vector<DeviceInfo> SimulatedEngine::ListDevices(DeviceKind kind)
{
    if (kind == DeviceKind::Microphone)
        return {{L"sim-mic-1", L"Demo Microphone Array", true},
                {L"sim-mic-2", L"Demo USB Microphone", false}};
    return {{L"sim-out-1", L"Demo Speakers", true},
            {L"sim-out-2", L"Demo Headphones", false}};
}

bool SimulatedEngine::Start(const RecordingSettings &settings, const std::wstring &outputPath,
                            std::wstring *error)
{
    if (IsRecording()) {
        if (error)
            *error = L"A recording is already running.";
        return false;
    }
    settings_ = settings;
    files_ = PlannedOutputFiles(settings, outputPath);
    failure_.clear();
    produced_[0] = produced_[1] = 0;
    startTime_ = endTime_ = now_();
    failAt_ = failAfter_;                   /* a failure applies to one recording only */
    failAfter_ = -1.0;
    recording_ = true;
    return true;
}

/* A scheduled failure takes effect the first time it is looked at after its
 * moment; the recording is cut at that moment, not at the time of looking. */
bool SimulatedEngine::IsRecording()
{
    if (recording_ && failAt_ >= 0.0 && now_() - startTime_ >= failAt_) {
        recording_ = false;
        endTime_ = startTime_ + failAt_;
        failure_ = failMessage_;
    }
    return recording_;
}

void SimulatedEngine::Stop()
{
    if (!IsRecording())
        return;
    recording_ = false;
    endTime_ = now_();
}

double SimulatedEngine::Elapsed()
{
    if (IsRecording())
        return std::max(0.0, now_() - startTime_);
    return endTime_ - startTime_;
}

RecordingStats SimulatedEngine::Stats()
{
    RecordingStats stats;
    stats.seconds = Elapsed();
    const double perFile = settings_.format == Format::Wav
                               ? kWavBytesPerSecond
                               : settings_.mp3BitrateKbps * 125.0;     /* kbit/s -> bytes/s */
    stats.bytesWritten = static_cast<std::uint64_t>(stats.seconds * perFile * static_cast<double>(files_.size()));
    return stats;
}

std::size_t SimulatedEngine::TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity)
{
    const int s = StreamIndex(stream);
    if (stream == StreamId::Microphone ? !settings_.includeMicrophone : !settings_.includeSystemAudio)
        return 0;

    const std::uint64_t due = static_cast<std::uint64_t>(Elapsed() * kPeakBinsPerSecond);
    if (due <= produced_[s])
        return 0;
    const std::size_t n = static_cast<std::size_t>(
        std::min<std::uint64_t>(capacity, due - produced_[s]));
    for (std::size_t i = 0; i < n; ++i)
        out[i] = Synthesize(stream, produced_[s] + i);
    produced_[s] += n;
    return n;
}

/* A repeatable pseudo-random number in [0, 1) for (a, b): splitmix64. */
double SimulatedEngine::Hash01(std::uint64_t a, std::uint64_t b) const
{
    std::uint64_t x = seed_ + a * 0x9E3779B97F4A7C15ull + b * 0xD1B54A32D192ED03ull;
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<double>(x >> 11) / 9007199254740992.0;
}

PeakBin SimulatedEngine::Synthesize(StreamId stream, std::uint64_t bin) const
{
    const double t = static_cast<double>(bin) / kPeakBinsPerSecond;
    double amp;

    if (stream == StreamId::Microphone) {
        /* Phrases of 2.4 s: most are "spoken" (a syllable rhythm under a
         * fade-in and fade-out), some are pauses with a little room noise. */
        const double position = t / 2.4;
        const double segment = std::floor(position);
        const double frac = position - segment;
        const std::uint64_t seg = static_cast<std::uint64_t>(segment);
        const bool speaking = Hash01(1, seg) > 0.30;
        const double edge = std::min(1.0, 8.0 * std::min(frac, 1.0 - frac));
        const double rate = 3.0 + 2.0 * Hash01(2, seg);
        const double s = std::sin(kPi * rate * t);
        const double syllable = 0.12 + 0.88 * s * s;
        const double jitter = Hash01(3, bin);
        amp = speaking ? 0.30 * edge * syllable * (0.7 + 0.3 * jitter) : 0.004 * jitter;
        amp *= 1.0 + settings_.micAmplifyPercent / 100.0;               /* gain, before clipping */
    } else {
        /* A steady beat (two a second) over a quieter bed. */
        const double beat = std::exp(-5.0 * std::fmod(t * 2.0, 1.0));
        amp = 0.42 * (0.30 + 0.70 * beat) * (0.85 + 0.15 * Hash01(4, bin));
        amp *= 1.0 + settings_.systemAmplifyPercent / 100.0;
    }

    PeakBin peak;
    peak.high = static_cast<float>(std::min(1.0, amp * (0.75 + 0.25 * Hash01(5, bin))));
    peak.low  = -static_cast<float>(std::min(1.0, amp * (0.75 + 0.25 * Hash01(6, bin))));
    return peak;
}

}  // namespace audiorgui
