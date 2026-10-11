/*
 * recorder_controller.h - The application logic behind the window.
 *
 * Owns the settings, one waveform lane per stream and the record/stop state
 * machine, and talks to an IRecordingEngine.  It knows nothing about
 * windows or drawing: the UI renders what the controller reports and
 * forwards the user's intent, and the tests drive it directly.
 *
 *     Idle ──Start()──▶ Recording ──Stop() / engine failure──▶ Idle
 *
 * The UI calls Tick() about 60 times a second with the time since the last
 * call.  Passing the time in (instead of reading a clock) keeps the
 * controller deterministic.
 *
 * Transcription rides along without being able to hurt the recording: its
 * text and status are pulled in Tick() beside the audio, and the recording's
 * own failure path (Outcome) never hears about transcription problems.
 *
 * So does the start-up set-up of transcription (see setup_status.h): the
 * controller polls it in Tick(), shows its notification until it is done or
 * dismissed, passes the user's answers on, and applies what it found out: with no
 * usable graphics card a recording does not ask for one, and new models are looked for.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "engine.h"
#include "settings.h"
#include "setup_status.h"
#include "transcript_lane.h"
#include "types.h"
#include "waveform_lane.h"

namespace audiorgui {

enum class Phase { Idle, Recording };

/* How the last recording (or attempt) turned out. */
struct Outcome {
    enum class Kind { None, Saved, Failed };

    Kind                      kind = Kind::None;
    std::wstring              message;      /* Failed: what went wrong          */
    std::vector<std::wstring> files;        /* what was written, even if Failed */
    std::vector<std::wstring> transcripts;  /* the transcript files, one for each transcribed stream that was spoken on */
    std::wstring              transcriptProblem;    /* a transcript file that could not be saved, in a few words */
};

class RecorderController {
public:
    using Clock = std::function<LocalTime()>;

    /* Whether a file is already on disk.  A recording never takes the name of
     * one that is: it gets " (2)", " (3)" ... instead.  Without it every name
     * is taken to be free (the engine still refuses to overwrite). */
    using FileExists = std::function<bool(const std::wstring &path)>;

    RecorderController(IRecordingEngine &engine, Clock clock, FileExists fileExists = nullptr);

    /* ---- settings (the UI edits them while Idle) ---- */
    RecordingSettings       &Settings()       { return settings_; }
    const RecordingSettings &Settings() const { return settings_; }
    bool SettingsLocked() const { return phase_ == Phase::Recording; }

    const std::vector<DeviceInfo> &Devices(DeviceKind kind) const;
    void RefreshDevices();

    /* Chooses the system default for a microphone or playback device the settings name that is not there
     * now (unplugged since the settings were saved).  Left alone, it would stop the recording from starting. */
    void DropUnavailableDevices();

    /* ---- recording ---- */
    Phase GetPhase() const { return phase_; }
    bool  CanStart() const { return phase_ == Phase::Idle; }
    bool  CanStop() const { return phase_ == Phase::Recording; }

    /* Returns false if recording did not start; LastOutcome() says why. */
    bool Start();
    void Stop();

    /* Advance time: pull new audio from the engine, notice a recording that
     * ended by itself, and move the waveforms along. */
    void Tick(double dtSeconds);

    /* True while Tick() still has something to do: a recording is running, or
     * a waveform has not settled yet.  The UI stops its frame timer when this
     * is false. */
    bool Animating() const;

    /* ---- what the UI shows ---- */
    bool                  StreamEnabled(StreamId stream) const;
    const WaveformLane   &Lane(StreamId stream) const { return lanes_[StreamIndex(stream)]; }
    RecordingStats        Stats() const { return stats_; }
    const Outcome        &LastOutcome() const { return outcome_; }
    bool                  IsSimulated() const { return engine_.IsSimulated(); }

    /* The settings in force: the snapshot while recording, else the draft. */
    const RecordingSettings &Active() const;

    /* ---- live transcripts ----
     * A transcript is kept per stream, like its waveform, and stays on show
     * after the recording until the next one starts. */
    bool CanTranscribe() const { return engine_.CanTranscribe(); }
    bool CanTranscribeOnGpu() const { return engine_.CanTranscribeOnGpu(); }

    /* The models to choose from, as of the start of the program. */
    const std::vector<TranscriptModel> &TranscriptModels() const { return transcriptModels_; }

    /* Whether the stream's pane shows a transcript: the engine can make one
     * and the stream's transcript is switched on. */
    bool ShowsTranscript(StreamId stream) const { return CanTranscribe() && Transcribes(Active(), stream); }

    const TranscriptLane &Transcript(StreamId stream) const { return transcripts_[StreamIndex(stream)]; }
    TranscriptStatus      TranscriptionStatus(StreamId stream) const { return transcriptStatus_[StreamIndex(stream)]; }
    const std::wstring   &TranscriptionProblem(StreamId stream) const { return transcriptProblem_[StreamIndex(stream)]; }

    /* Looks again for models (after the set-up downloaded one) and takes up what it finds. */
    void RefreshTranscription();

    /* What the start-up scan found out about the graphics card: when it found none to use, a recording
     * does not use one, and the GPU switch is off and cannot be turned on.  The setting itself, which is
     * the user's choice and is saved, is left as it is. */
    bool GpuUnusable() const { return gpuUnusable_; }

    /* The user's "not now" to the offer to download a speech model, for the settings file to keep. */
    bool DownloadDeclined() const { return downloadDeclined_; }
    void SetDownloadDeclined(bool declined) { downloadDeclined_ = declined; }

    /* ---- the start-up set-up of transcription ----
     * Nothing is attached (and HasSetup() is false) unless the transcription helper is installed.  The
     * source must outlive the controller. */
    void AttachSetup(ISetupSource *source) { setupSource_ = source; }
    bool HasSetup() const { return setupSource_ != nullptr; }

    const SetupStatus &Setup() const { return setup_; }

    /* Whether the notification is on show: the set-up has something to say and the user has not dismissed it. */
    bool SetupVisible() const { return setup_.phase != SetupPhase::Idle && !setupDismissed_; }

    /* Whether Tick() has to keep running for the set-up: it is working, or its answer is awaited, or the
     * notification is about to go away by itself. */
    bool SetupBusy() const;

    /* Changes whenever Setup() or SetupVisible() does, so that the view rebuilds its text only then. */
    std::uint32_t SetupVersion() const { return setupVersion_; }

    void AnswerSetup(bool download);        /* the answer to the question of the Asking phase */
    void CancelSetup();                     /* ends the download */
    void RetrySetup();                      /* asks again, after a "not now" or a failure */
    void DismissSetup();                    /* the notification goes away (it also goes by itself, a few seconds after a success) */

private:
    std::wstring NextOutputPath();
    void DrainPeaks();
    void DrainTranscripts();
    void PollSetup(double dtSeconds);
    bool TakeSetup();
    void Finish();

    bool AnyPlannedFileExists(const std::wstring &path) const;
    /* The transcript files this recording will make: none where the engine cannot transcribe, however the
     * (saved) switches stand: their boxes are hidden then, so they cannot be switched off. */
    std::vector<std::wstring> PlannedTranscripts(const std::wstring &path) const;

    IRecordingEngine  &engine_;
    Clock              clock_;
    FileExists         fileExists_;
    RecordingSettings  settings_;
    RecordingSettings  active_;
    Phase              phase_ = Phase::Idle;
    WaveformLane       lanes_[kStreamCount];
    TranscriptLane     transcripts_[kStreamCount];
    TranscriptStatus   transcriptStatus_[kStreamCount];
    std::wstring       transcriptProblem_[kStreamCount];
    RecordingStats     stats_;
    Outcome            outcome_;
    std::vector<DeviceInfo> devices_[2];
    std::vector<TranscriptModel> transcriptModels_;

    ISetupSource *setupSource_ = nullptr;
    SetupStatus   setup_;
    std::uint32_t setupVersion_ = 0;
    bool          setupDismissed_ = false;
    double        setupShownSeconds_ = 0.0;     /* how long a finished set-up has been on show */
    double        setupAwaitSeconds_ = 0.0;     /* how much longer to keep polling for the answer to a request */
    bool          gpuUnusable_ = false;
    bool          downloadDeclined_ = false;
    bool          modelsApplied_ = false;        /* this run's new models have been looked for (reset by RetrySetup) */
};

}  // namespace audiorgui
