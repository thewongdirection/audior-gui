#include "recorder_controller.h"

#include <utility>

namespace audiorgui {

namespace {

constexpr std::size_t kPullChunk     = 256;     /* bins per TakePeaks() call            */
constexpr int         kMaxPullsPerTick = 64;    /* bounds a misbehaving engine's output */

}  // namespace

RecorderController::RecorderController(IRecordingEngine &engine, Clock clock)
    : engine_(engine), clock_(std::move(clock))
{
    RefreshDevices();
}

const std::vector<DeviceInfo> &RecorderController::Devices(DeviceKind kind) const
{
    return devices_[kind == DeviceKind::Microphone ? 0 : 1];
}

void RecorderController::RefreshDevices()
{
    devices_[0] = engine_.ListDevices(DeviceKind::Microphone);
    devices_[1] = engine_.ListDevices(DeviceKind::Playback);
}

const RecordingSettings &RecorderController::Active() const
{
    return phase_ == Phase::Recording ? active_ : settings_;
}

bool RecorderController::StreamEnabled(StreamId stream) const
{
    const RecordingSettings &s = Active();
    return stream == StreamId::Microphone ? s.includeMicrophone : s.includeSystemAudio;
}

/* "audior-<time>.<ext>" in the output folder.  The time has one-second
 * resolution, so a recording restarted within the same second gets a "-2",
 * "-3" ... suffix rather than overwriting the one before. */
std::wstring RecorderController::NextOutputPath()
{
    std::wstring base = DefaultBaseName(clock_());
    if (base == lastBaseName_) {
        ++repeatCount_;
        base += L"-" + std::to_wstring(repeatCount_ + 1);
    } else {
        lastBaseName_ = base;
        repeatCount_ = 0;
    }
    return JoinPath(settings_.outputDirectory, base + ExtensionFor(settings_.format));
}

bool RecorderController::Start()
{
    if (!CanStart())
        return false;

    outcome_ = Outcome();
    const std::wstring problem = Validate(settings_);
    if (!problem.empty()) {
        outcome_.kind = Outcome::Kind::Failed;
        outcome_.message = problem;
        return false;
    }

    std::wstring error;
    if (!engine_.Start(settings_, NextOutputPath(), &error)) {
        outcome_.kind = Outcome::Kind::Failed;
        outcome_.message = error.empty() ? L"The recording could not be started." : error;
        return false;
    }

    active_ = settings_;
    stats_ = RecordingStats();
    for (WaveformLane &lane : lanes_)
        lane.Reset();
    phase_ = Phase::Recording;
    return true;
}

void RecorderController::Stop()
{
    if (!CanStop())
        return;
    engine_.Stop();
    Finish();
}

void RecorderController::DrainPeaks()
{
    PeakBin chunk[kPullChunk];
    for (int s = 0; s < kStreamCount; ++s) {
        for (int pulls = 0; pulls < kMaxPullsPerTick; ++pulls) {
            const std::size_t n = engine_.TakePeaks(static_cast<StreamId>(s), chunk, kPullChunk);
            if (n == 0)
                break;
            lanes_[s].Feed(chunk, n);
        }
    }
}

/* The recording is over (stopped, or it failed by itself): collect the last
 * audio and the result. */
void RecorderController::Finish()
{
    DrainPeaks();
    stats_ = engine_.Stats();

    const std::wstring failure = engine_.Failure();
    outcome_.kind = failure.empty() ? Outcome::Kind::Saved : Outcome::Kind::Failed;
    outcome_.message = failure;
    outcome_.files = engine_.OutputFiles();
    phase_ = Phase::Idle;
}

bool RecorderController::Animating() const
{
    if (phase_ == Phase::Recording)
        return true;
    for (const WaveformLane &lane : lanes_) {
        if (lane.Active())
            return true;
    }
    return false;
}

void RecorderController::Tick(double dtSeconds)
{
    if (phase_ == Phase::Recording) {
        DrainPeaks();
        stats_ = engine_.Stats();
        if (!engine_.IsRecording()) {
            engine_.Stop();         /* lets the engine release and finalise what it holds */
            Finish();
        }
    }
    for (WaveformLane &lane : lanes_)
        lane.Advance(dtSeconds);
}

}  // namespace audiorgui
