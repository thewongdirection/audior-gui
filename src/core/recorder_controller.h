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
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "engine.h"
#include "settings.h"
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

private:
    std::wstring NextOutputPath();
    void DrainPeaks();
    void Finish();

    bool AnyPlannedFileExists(const std::wstring &path) const;

    IRecordingEngine  &engine_;
    Clock              clock_;
    FileExists         fileExists_;
    RecordingSettings  settings_;
    RecordingSettings  active_;
    Phase              phase_ = Phase::Idle;
    WaveformLane       lanes_[kStreamCount];
    RecordingStats     stats_;
    Outcome            outcome_;
    std::vector<DeviceInfo> devices_[2];
};

}  // namespace audiorgui
