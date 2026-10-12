#include "recorder_controller.h"

#include <algorithm>
#include <utility>

namespace audiorgui {

namespace {

constexpr std::size_t kPullChunk     = 256;     /* bins per TakePeaks() call            */
constexpr std::size_t kTranscriptChunk = 4;     /* events per TakeTranscript() call     */
constexpr int         kMaxPullsPerTick = 64;    /* bounds a misbehaving engine's output */
constexpr int         kMaxNumberedNames = 1000; /* " (2)" ... " (999)" before giving up   */
constexpr double      kSetupResultSeconds = 10.0; /* how long the notification of a finished set-up stays   */
constexpr double      kSetupAwaitSeconds = 2.0;  /* how long to wait for the set-up to answer a request  */

}  // namespace

RecorderController::RecorderController(IRecordingEngine &engine, Clock clock, FileExists fileExists)
    : engine_(engine), clock_(std::move(clock)), fileExists_(std::move(fileExists)),
      transcriptModels_(engine.TranscriptModels())
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

void RecorderController::DropUnavailableDevices()
{
    const auto listed = [](const std::vector<DeviceInfo> &devices, const std::wstring &id) {
        for (const DeviceInfo &d : devices) {
            if (d.id == id)
                return true;
        }
        return false;
    };
    if (!listed(Devices(DeviceKind::Microphone), settings_.microphoneId))
        settings_.microphoneId.clear();
    if (!listed(Devices(DeviceKind::Playback), settings_.systemDeviceId))
        settings_.systemDeviceId.clear();
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

std::vector<std::wstring> RecorderController::PlannedTranscripts(const std::wstring &path) const
{
    return engine_.CanTranscribe() ? PlannedTranscriptFiles(settings_, path) : std::vector<std::wstring>();
}

std::vector<std::wstring> RecorderController::PlannedEnhanced(const std::wstring &path) const
{
    return engine_.CanEnhanceVoice() ? PlannedEnhancedFiles(settings_, path) : std::vector<std::wstring>();
}

bool RecorderController::AnyPlannedFileExists(const std::wstring &path) const
{
    for (const std::vector<std::wstring> &files :       /* a transcript and an enhanced copy keep their recording's name */
         {PlannedOutputFiles(settings_, path), PlannedTranscripts(path), PlannedEnhanced(path)}) {
        for (const std::wstring &file : files) {
            if (fileExists_(file))
                return true;
        }
    }
    return false;
}

/* The file to record to: the name the user typed (given the format's
 * extension), else "Audior-GUI-<time>", in the output folder.  A recording never
 * replaces an existing file, so a taken name gets " (2)", " (3)" ... before
 * its extension; with two tracks, a taken "_mic" or "_system" file counts, and
 * so does a taken transcript file or enhanced copy, so that a recording and what goes with it are numbered alike. */
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
    if (Enhancing())                                    /* recording comes first; the copy would have to wait */
        CancelEnhancement();

    outcome_ = Outcome();
    const std::wstring problem = Validate(settings_);
    if (!problem.empty()) {
        outcome_.kind = Outcome::Kind::Failed;
        outcome_.message = problem;
        return false;
    }

    const std::wstring path = NextOutputPath();
    const std::wstring limit = std::to_wstring(kMaxOutputPathChars);
    const auto tooLong = [&](const std::vector<std::wstring> &files, const wchar_t *what, const wchar_t *advice) {
        if (std::none_of(files.begin(), files.end(), [](const std::wstring &f) { return f.size() > kMaxOutputPathChars; }))
            return false;
        outcome_.kind = Outcome::Kind::Failed;
        outcome_.message = std::wstring(what) + L" too long (the limit is " + limit + L" characters). " + advice;
        return true;
    };
    if (tooLong(PlannedOutputFiles(settings_, path), L"The output path is", L"Choose a shorter folder or file name.") ||
        tooLong(PlannedTranscripts(path), L"The transcript's path would be",       /* their names are the longer ones */
                L"Choose a shorter folder or file name, or switch the Transcript off.") ||
        tooLong(PlannedEnhanced(path), L"The enhanced copy's path would be",
                L"Choose a shorter folder or file name, or switch Enhance voice off."))
        return false;

    RecordingSettings run = settings_;
    if (!engine_.CanTranscribe())                       /* no transcript in this recording: a model that arrives during it is for the next */
        run.transcribeMicrophone = run.transcribeSystem = false;
    if (gpuUnusable_)                                   /* what the scan found out about this PC, not the user's choice */
        run.transcribeGpu = false;
    run.enhanceVoice = run.enhanceVoice && engine_.CanEnhanceVoice();
    std::wstring error;
    if (!engine_.Start(run, path, &error)) {
        outcome_.kind = Outcome::Kind::Failed;
        outcome_.message = error.empty() ? L"The recording could not be started." : error;
        return false;
    }

    active_ = run;
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
    outcome_.transcripts = engine_.TranscriptFiles();
    outcome_.transcriptProblem = engine_.TranscriptFileProblem();
    phase_ = Phase::Idle;

    if (active_.enhanceVoice && outcome_.kind == Outcome::Kind::Saved) {        /* the engine has begun the clearer copies */
        const EnhanceStatus status = engine_.Enhancement();
        if (status.state == EnhanceState::Working) {
            enhanceProgress_ = status.progress;
            phase_ = Phase::Enhancing;
        } else {
            TakeEnhancement(status);
        }
    }
}

void RecorderController::PollEnhancement()
{
    const EnhanceStatus status = engine_.Enhancement();
    enhanceProgress_ = status.progress;
    if (status.state != EnhanceState::Working) {
        TakeEnhancement(status);
        phase_ = Phase::Idle;
    }
}

/* The enhancement is over (done, failed or given up): what it made goes into the outcome. */
void RecorderController::TakeEnhancement(const EnhanceStatus &status)
{
    outcome_.enhancement = status.state;
    outcome_.enhanceProblem = status.problem;
    outcome_.enhanced = engine_.EnhancedFiles();
}

void RecorderController::Shutdown()
{
    Stop();
    CancelEnhancement();            /* (the one Stop() has just begun, if the recording asked for it, too) */
}

void RecorderController::CancelEnhancement()
{
    if (!Enhancing())
        return;
    engine_.CancelEnhancement();
    EnhanceStatus status = engine_.Enhancement();
    if (status.state == EnhanceState::Working)          /* an engine that did not stop: it is given up all the same */
        status.state = EnhanceState::Cancelled;
    TakeEnhancement(status);
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

bool RecorderController::SetupBusy() const
{
    if (!setupSource_)
        return false;
    switch (setup_.phase) {
    case SetupPhase::Scanning:
    case SetupPhase::Downloading:
        return true;
    case SetupPhase::Finished:                  /* a failure stays until it is dismissed: nothing to count down */
        return setupAwaitSeconds_ > 0.0 || (!setupDismissed_ && setup_.result != SetupResult::Failed);
    default:
        return setupAwaitSeconds_ > 0.0;
    }
}

void RecorderController::RefreshTranscription()
{
    engine_.RescanTranscription();
    transcriptModels_ = engine_.TranscriptModels();
}

void RecorderController::AnswerSetup(bool download)
{
    if (!setupSource_)
        return;
    setupSource_->Answer(download);
    setupAwaitSeconds_ = kSetupAwaitSeconds;
}

void RecorderController::CancelSetup()
{
    if (!setupSource_)
        return;
    setupSource_->Cancel();
    setupAwaitSeconds_ = kSetupAwaitSeconds;
}

void RecorderController::RetrySetup()
{
    if (!setupSource_)
        return;
    modelsApplied_ = false;                             /* a new run finds out afresh */
    setupSource_->Retry();
    setupAwaitSeconds_ = kSetupAwaitSeconds;
    TakeSetup();                                        /* it says Scanning at once: no look at the old notification */
}

void RecorderController::DismissSetup()
{
    setupDismissed_ = true;
    ++setupVersion_;
}

/* Takes what the set-up has to say, if it is something new, and applies what it found out. */
bool RecorderController::TakeSetup()
{
    if (!setupSource_->Poll(&setup_))
        return false;
    ++setupVersion_;
    setupDismissed_ = false;
    setupShownSeconds_ = 0.0;
    setupAwaitSeconds_ = 0.0;
    downloadDeclined_ = setup_.declined;
    if (setup_.scanned)
        gpuUnusable_ = !setup_.gpuUsable;
    if (setup_.modelsChanged && !modelsApplied_) {
        modelsApplied_ = true;
        RefreshTranscription();
    }
    return true;
}

void RecorderController::PollSetup(double dtSeconds)
{
    if (!setupSource_ || TakeSetup())
        return;

    setupAwaitSeconds_ = std::max(0.0, setupAwaitSeconds_ - dtSeconds);
    if (setup_.phase == SetupPhase::Finished && !setupDismissed_ && setup_.result != SetupResult::Failed) {
        setupShownSeconds_ += dtSeconds;
        if (setupShownSeconds_ >= kSetupResultSeconds)
            DismissSetup();
    }
}

void RecorderController::Tick(double dtSeconds)
{
    PollSetup(dtSeconds);
    if (phase_ == Phase::Enhancing)
        PollEnhancement();
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
