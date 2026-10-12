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
 * TakePeaks().  What an engine reports must be what it records: each stream
 * after the microphone gain (so clipping shows up in the waveform) and before
 * any merge weighting, clamped to -1..+1 as it is written.
 *
 * The controller needs the engine to be usable (COM initialised, devices
 * listable) as soon as it is constructed, and it never overwrites a file it
 * did not create: Start() must fail, with a message, rather than replace an
 * existing file.
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "settings.h"
#include "transcript.h"
#include "types.h"

namespace audiorgui {

/* How the enhancement of the voice after a recording stands (see IRecordingEngine::Enhancement). */
enum class EnhanceState { Off, Working, Done, Failed, Cancelled };

struct EnhanceStatus {
    EnhanceState state    = EnhanceState::Off;
    float        progress = 0.0f;       /* 0 to 1 of all the files, while Working */
    std::wstring problem;               /* Failed: what went wrong, in a few words */
};

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

    /* Stop and finalise the file(s).  Does nothing when not recording.  The
     * controller also calls it after IsRecording() went false by itself, so
     * an engine can release what it still holds.  It runs on the UI thread
     * and may take a moment (closing devices, flushing the encoder). */
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

    /* Why the last recording failed, or empty.  That includes a failure to
     * finalise the files after a normal Stop() (a full disk on the last
     * flush, say), not only one that ended the recording by itself. */
    virtual std::wstring Failure() = 0;

    /* The files the current or last recording wrote. */
    virtual std::vector<std::wstring> OutputFiles() = 0;

    /* ---- live transcript (optional) -------------------------------------
     * An engine that can turn a stream's speech into text says so with
     * CanTranscribe(); the others keep these defaults.  Start() receives which
     * streams to transcribe in its settings (see Transcribes()).  The text
     * reaches the GUI the way peaks do, pulled with TakeTranscript() (see
     * transcript.h for the events).  Trouble with transcription never ends the
     * recording: it only turns that stream's status into Failed. */
    virtual bool CanTranscribe() const { return false; }

    /* The models it can use, the one an empty RecordingSettings::transcribeModel
     * stands for first; empty when it cannot transcribe. */
    virtual std::vector<TranscriptModel> TranscriptModels() const { return {}; }

    /* Looks again for what transcription needs (a model that was downloaded since the program
     * started).  Afterwards CanTranscribe() and TranscriptModels() answer for what is there now. */
    virtual void RescanTranscription() {}

    /* True if it can also transcribe on the graphics card (RecordingSettings::transcribeGpu). */
    virtual bool CanTranscribeOnGpu() const { return false; }

    /* Where the stream's transcription stands.  Off for a stream that is not
     * being transcribed, and for every stream once the recording has stopped. */
    virtual TranscriptStatus Transcription(StreamId stream)
    {
        (void)stream;
        return TranscriptStatus();
    }

    /* Why the stream's transcription is Failed. */
    virtual std::wstring TranscriptionProblem(StreamId stream)
    {
        (void)stream;
        return std::wstring();
    }

    /* Move up to `capacity` transcript events that have arrived since the last
     * call into `out`; returns how many. */
    virtual std::size_t TakeTranscript(StreamId stream, TranscriptEvent *out, std::size_t capacity)
    {
        (void)stream;
        (void)out;
        (void)capacity;
        return 0;
    }

    /* The text files the current or last recording saved its transcripts in, one for each transcribed stream
     * that was spoken on (see PlannedTranscriptFile(); a simulated engine only names them), complete once
     * Stop() has returned.  Empty if there were none. */
    virtual std::vector<std::wstring> TranscriptFiles() { return {}; }

    /* A sentence for the user if a transcript file could not be saved (the recording itself is not
     * affected), else empty. */
    virtual std::wstring TranscriptFileProblem() { return std::wstring(); }

    /* ---- voice enhancement (optional) -----------------------------------
     * An engine that can write a clearer copy of what it recorded says so with CanEnhanceVoice().  With
     * RecordingSettings::enhanceVoice on, a recording that ended well is followed by that work (see
     * voice_enhancer.h), which the engine does on a thread of its own: Stop() still returns at once, and
     * Enhancement() tells how far the work has got.  The copy of each file is named by EnhancedPath(), the
     * recorded file is not touched, and nothing is ever overwritten.  Trouble with it never harms the recording. */
    virtual bool CanEnhanceVoice() const { return false; }

    /* Off if the last recording is not being enhanced. */
    virtual EnhanceStatus Enhancement() { return EnhanceStatus(); }

    /* Stops the work, waiting for it to end, and removes the copy it was writing.  Does nothing when not working. */
    virtual void CancelEnhancement() {}

    /* The copies made so far by the last recording's enhancement, complete once it is no longer Working. */
    virtual std::vector<std::wstring> EnhancedFiles() { return {}; }
};

}  // namespace audiorgui
