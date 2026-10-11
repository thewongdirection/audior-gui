/*
 * simulated_engine.h - An IRecordingEngine that makes up the audio.
 *
 * Used for the demo mode (so the window can be tried and developed without a
 * microphone) and by the tests.  It writes no files.  The microphone plays a
 * speech-like signal and the system stream a beat-like one; both follow the
 * recording settings the way the real engine would (microphone gain, clipping,
 * which streams exist, file count and size).
 *
 * Time comes from an injected clock and every bin is a pure function of its
 * index, so a run is reproducible.
 *
 * It also plays a live transcript: a short conversation, spoken word by word
 * (each utterance as partials, one of them revised, then final), on a loop,
 * so the demo and the tests can exercise everything a transcribing engine
 * does.  The script depends on the recording time only.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../core/engine.h"

namespace audiorgui {

class SimulatedEngine : public IRecordingEngine {
public:
    using Clock = std::function<double()>;      /* seconds, monotonic */

    explicit SimulatedEngine(Clock nowSeconds, std::uint64_t seed = 1);

    /* Make the next recording (only) fail by itself after `seconds`, as an
     * unplugged microphone would (for demos and tests). */
    void FailAfter(double seconds, std::wstring message);

    /* The same for the transcription alone: it fails after `seconds` of the
     * next recording, which carries on. */
    void FailTranscriptAfter(double seconds, std::wstring message);

    bool IsSimulated() const override { return true; }
    std::vector<DeviceInfo> ListDevices(DeviceKind kind) override;
    bool Start(const RecordingSettings &settings, const std::wstring &outputPath,
               std::wstring *error) override;
    void Stop() override;
    bool IsRecording() override;
    RecordingStats Stats() override;
    std::size_t TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity) override;
    std::wstring Failure() override { return failure_; }
    std::vector<std::wstring> OutputFiles() override { return files_; }

    bool CanTranscribe() const override { return true; }
    std::vector<TranscriptModel> TranscriptModels() const override;     /* the table's, as if all were installed */
    TranscriptStatus Transcription(StreamId stream) override;
    std::wstring TranscriptionProblem(StreamId) override { return transcriptFailMessage_; }
    std::size_t TakeTranscript(StreamId stream, TranscriptEvent *out, std::size_t capacity) override;

private:
    double Elapsed();
    double TranscriptElapsed();
    PeakBin Synthesize(StreamId stream, std::uint64_t bin) const;
    double Hash01(std::uint64_t a, std::uint64_t b) const;

    Clock         now_;
    std::uint64_t seed_;

    bool          recording_ = false;
    double        startTime_ = 0.0;
    double        endTime_   = 0.0;         /* when the recording stopped or failed */
    std::uint64_t produced_[kStreamCount] = {0, 0};
    RecordingSettings settings_;
    std::vector<std::wstring> files_;
    std::wstring  failure_;

    double        failAfter_ = -1.0;        /* armed for the next Start(); < 0: none */
    double        failAt_    = -1.0;        /* when the current recording fails; < 0: never */
    std::wstring  failMessage_;

    std::uint64_t transcriptNext_[kStreamCount] = {0, 0};   /* the next scripted event of each stream */
    double        transcriptFailAfter_ = -1.0;              /* the same two for the transcription */
    double        transcriptFailAt_    = -1.0;
    std::wstring  transcriptFailMessage_;
};

}  // namespace audiorgui
