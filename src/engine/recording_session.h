/*
 * recording_session.h - Runs one recording: a capture thread per stream, one
 * writer thread, the output files.
 *
 *   capture thread (one per stream)       writer thread
 *     source->Poll() every 10 ms            every 100 ms: read the rings,
 *     -> peak bins for the waveform           mix if merging, write to the
 *     -> ring buffer (8 s)                    sinks
 *
 * The rings are the only place audio waits, so memory stays bounded however
 * long the recording runs.  Anything that goes wrong (a device that vanishes,
 * a disk that fills, a writer that cannot keep up) ends the recording at once;
 * what was captured up to then is finalised into valid files and Failure()
 * says what happened.
 *
 * Sources and sinks come from factories, so the whole pipeline is tested with
 * generated audio and no hardware.
 */
#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/settings.h"
#include "../core/types.h"
#include "audio_sink.h"
#include "capture_source.h"
#include "speech_tap.h"

namespace audiorgui {

/* What a recording captures and where it goes. */
struct SessionPlan {
    bool  microphone = true;
    bool  system     = false;
    bool  merge      = false;               /* both streams mixed into files[0]    */
    float micWeight  = 0.65f;               /* the microphone's share when merging */

    /* The streams (a bit per StreamId) whose audio also goes to the speech
     * sink, if the session has one. */
    std::uint8_t speech = 0;

    /* One file; or, for two streams kept apart, the microphone's then the
     * system audio's. */
    std::vector<std::wstring> files;
};

SessionPlan MakePlan(const RecordingSettings &settings, const std::wstring &outputPath);

class RecordingSession {
public:
    using SourceFactory = std::function<std::unique_ptr<ICaptureSource>(StreamId)>;
    using SinkFactory   = std::function<std::unique_ptr<IAudioSink>(const std::wstring &path, HRESULT *hr)>;

    /* `externalStop`, if given, is an event that another process may set to end
     * the recording; it must outlive the session.  `speech`, if given, gets the
     * audio of the streams in SessionPlan::speech (16 kHz mono, from the capture
     * threads); it must outlive the session too. */
    RecordingSession(SourceFactory sources, SinkFactory sinks, HANDLE externalStop = nullptr,
                     ISpeechSink *speech = nullptr);
    ~RecordingSession();

    RecordingSession(const RecordingSession &) = delete;
    RecordingSession &operator=(const RecordingSession &) = delete;

    /* Opens the devices and the files and starts recording.  On failure
     * returns false, says why in *error, and leaves nothing behind (no files,
     * no open devices).  A session records once. */
    bool Start(const SessionPlan &plan, std::wstring *error);

    /* Ends the recording and finalises the files; waits until that is done.
     * Safe to call again. */
    void Stop();

    /* True from a successful Start() until the recording is asked to stop or
     * fails. */
    bool Running() const { return started_ && !stopping_; }

    RecordingStats Stats() const;
    std::size_t    TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity);
    std::wstring   Failure() const;
    std::vector<std::wstring> Files() const { return started_ ? plan_.files : std::vector<std::wstring>(); }

private:
    struct Stream;
    struct Output;

    bool OpenOutputs(std::wstring *error);
    void CaptureLoop(int index);
    HRESULT OpenSource(Stream &stream);
    void PollLoop(Stream &stream);
    void Deliver(Stream &stream, const std::vector<std::int16_t> &frames, std::vector<PeakBin> *bins);
    void WriteLoop();
    void Drain(bool final);
    void Finalise();
    void Fail(const wchar_t *what, long hr);
    void RequestStop();
    void Join();
    void Abort();

    SourceFactory makeSource_;
    SinkFactory   makeSink_;
    HANDLE        externalStop_;
    ISpeechSink  *speech_;

    SessionPlan   plan_;
    HANDLE        stop_ = nullptr;                      /* set to end the recording */
    std::atomic<bool> stopping_{false};
    bool          started_ = false;

    std::unique_ptr<Stream> streams_[kStreamCount];
    std::vector<Output>     outputs_;
    std::thread             writer_;
    std::atomic<int>        liveCaptures_{0};
    std::atomic<std::uint64_t> framesWritten_{0};
    bool                    sinkFailed_ = false;        /* writer thread only */
    std::vector<std::int16_t> scratchA_, scratchB_;     /* writer thread only */

    std::mutex              readyMutex_;                /* the devices-are-open handshake */
    std::condition_variable readyCv_;
    int                     ready_ = 0;

    mutable std::mutex      failureMutex_;
    std::wstring            failure_;
};

}  // namespace audiorgui
