/*
 * engine.h - The seam between the GUI and whatever records the audio.
 *
 * The GUI never touches audio itself.  It drives an IRecordingEngine, which
 * is either the simulation used for demos and tests, or the native WASAPI
 * engine (microphone and loopback capture, WAV and Media Foundation MP3
 * output).  All calls come from the UI thread; an implementation does its own
 * locking if it records on other threads.
 *
 * Audio reaches the GUI only as peak bins (see PeakBin), pulled with
 * TakePeaks().  What an engine reports must be what it records: after the
 * microphone gain, so that clipping shows up in the waveform.
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "settings.h"
#include "types.h"

namespace audiorgui {

class IRecordingEngine {
public:
    virtual ~IRecordingEngine() = default;

    /* True for engines that write nothing and play back made-up audio. */
    virtual bool IsSimulated() const = 0;

    /* Active devices of one kind; the system default is flagged. */
    virtual std::vector<DeviceInfo> ListDevices(DeviceKind kind) = 0;

    /* Start recording to `outputPath` (the file name includes the extension
     * for settings.format).  On failure returns false and says why in
     * *error.  The settings are copied; later changes do not affect the
     * recording. */
    virtual bool Start(const RecordingSettings &settings, const std::wstring &outputPath,
                       std::wstring *error) = 0;

    /* Stop and finalise the file(s).  Does nothing when not recording. */
    virtual void Stop() = 0;

    /* True while recording.  Becomes false by itself if the recording fails
     * (a full disk, an unplugged microphone); see Failure(). */
    virtual bool IsRecording() = 0;

    /* Time and size of the current recording, or of the last one. */
    virtual RecordingStats Stats() = 0;

    /* Move up to `capacity` peak bins that have arrived since the last call
     * into `out`; returns how many.  Streams that are not being recorded
     * deliver none. */
    virtual std::size_t TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity) = 0;

    /* Why the last recording ended on its own, or empty. */
    virtual std::wstring Failure() = 0;

    /* The files the current or last recording wrote. */
    virtual std::vector<std::wstring> OutputFiles() = 0;
};

}  // namespace audiorgui
