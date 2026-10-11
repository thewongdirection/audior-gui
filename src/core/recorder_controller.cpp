#include "recorder_controller.h"

#include <utility>

namespace audiorgui {

namespace {

constexpr std::size_t kPullChunk     = 256;     /* bins per TakePeaks() call            */
constexpr std::size_t kTranscriptChunk = 4;     /* events per TakeTranscript() call     */
constexpr int         kMaxPullsPerTick = 64;    /* bounds a misbehaving engine's output */
constexpr int         kMaxNumberedNames = 1000; /* " (2)" ... " (999)" before giving up   */

}  // namespace

RecorderController::RecorderController(IRecordingEngine &engine, Clock clock, FileExists fileExists)
    : engine_(engine), clock_(std::move(clock)), fileExists_(std::move(fileExists))
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

bool RecorderController::AnyPlannedFileExists(const std::wstring &path) const
{
    for (const std::wstring &file : PlannedOutputFiles(settings_, path)) {
        if (fileExists_(file))
            return true;
    }
    return false;
}

/* The file to record to: the name the user typed (given the format's
 * extension), else "Audior-GUI-<time>", in the output folder.  A recording never
 * replaces an existing file, so a taken name gets " (2)", " (3)" ... before
 * its extension; with two tracks, a taken "_mic" or "_system" file counts. */
std::wstring RecorderController::NextOutputPath()
{
    const std::wstring name = settings_.fileName.empty()
                                  ? DefaultBaseName(clock_()) + ExtensionFor(settings_.format)
                                  : EnforceExtension(settings_.fileName, settings_.format);
    std::wstring path = JoinPath(settings_.outputDirectory, name);
    if (!fileExists_)
        return path;

    const std::size_t dot = name.rfind(L'.');           /* the extension is always there */
    const std::wstring stem = name.substr(0, dot), extension = name.substr(dot);
    for (int n = 2; n < kMaxNumberedNames && AnyPlannedFileExists(path); ++n)
        path = JoinPath(settings_.outputDirectory, stem + L" (" + std::to_wstring(n) + L")" + extension);
    return path;
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

    const std::wstring path = NextOutputPath();
    for (const std::wstring &file : PlannedOutputFiles(settings_, path)) {
        if (file.size() > kMaxOutputPathChars) {
            outcome_.kind = Outcome::Kind::Failed;
            outcome_.message = L"The output path is too long (the limit is " + std::to_wstring(kMaxOutputPathChars) +
                               L" characters). Choose a shorter folder or file name.";
            return false;
        }
    }

    std::wstring error;
    if (!engine_.Start(settings_, path, &error)) {
        outcome_.kind = Outcome::Kind::Failed;
        outcome_.message = error.empty() ? L"The recording could not be started." : error;
        return false;
    }

    active_ = settings_;
    stats_ = RecordingStats();
    for (WaveformLane &lane : lanes_)
        lane.Reset();
    for (int s = 0; s < kStreamCount; ++s) {
        transcripts_[s].Reset();
        transcriptStatus_[s] = TranscriptStatus();
        transcriptProblem_[s].clear();
    }
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

/* New transcript text and each transcribed stream's status.  A stream whose
 * transcription failed stays marked until the next recording. */
void RecorderController::DrainTranscripts()
{
    if (!Transcribes(active_, StreamId::Microphone) && !Transcribes(active_, StreamId::System))
        return;

    TranscriptEvent chunk[kTranscriptChunk];
    for (int s = 0; s < kStreamCount; ++s) {
        const StreamId stream = static_cast<StreamId>(s);
        if (!Transcribes(active_, stream))
            continue;
        for (int pulls = 0; pulls < kMaxPullsPerTick; ++pulls) {
            const std::size_t n = engine_.TakeTranscript(stream, chunk, kTranscriptChunk);
            if (n == 0)
                break;
            for (std::size_t i = 0; i < n; ++i)
                transcripts_[s].Apply(chunk[i]);
        }
        if (transcriptStatus_[s].state != TranscriptState::Failed) {
            transcriptStatus_[s] = engine_.Transcription(stream);
            if (transcriptStatus_[s].state == TranscriptState::Failed)
                transcriptProblem_[s] = engine_.TranscriptionProblem(stream);
        }
    }
}

/* The recording is over (stopped, or it failed by itself): collect the last
 * audio and the result. */
void RecorderController::Finish()
{
    DrainPeaks();
    DrainTranscripts();
    for (TranscriptLane &transcript : transcripts_)
        transcript.Settle();
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
    for (const TranscriptLane &transcript : transcripts_) {
        if (transcript.Active())
            return true;
    }
    return false;
}

void RecorderController::Tick(double dtSeconds)
{
    if (phase_ == Phase::Recording) {
        DrainPeaks();
        DrainTranscripts();
        stats_ = engine_.Stats();
        if (!engine_.IsRecording()) {
            engine_.Stop();         /* lets the engine release and finalise what it holds */
            Finish();
        }
    }
    for (WaveformLane &lane : lanes_)
        lane.Advance(dtSeconds);
    for (TranscriptLane &transcript : transcripts_)
        transcript.Advance(dtSeconds);
}

}  // namespace audiorgui
