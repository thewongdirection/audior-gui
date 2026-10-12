/*
 * wasapi_engine.h - The real recording engine: WASAPI capture (microphone and
 * loopback), WAV or Media Foundation MP3 output.
 *
 * The audio work happens on the session's own threads; this class adapts it to
 * IRecordingEngine, owns the stop signal other processes use to end a
 * recording, and builds the sources and sinks for one recording from its
 * settings.
 */
#pragma once

#include <memory>

#include "../core/engine.h"
#include "asr_helper.h"
#include "recording_session.h"
#include "stop_signal.h"
#include "transcript_file.h"
#include "transcription_host.h"
#include "voice_enhance_job.h"

namespace audiorgui {

class WasapiEngine final : public IRecordingEngine {
public:
    /* Transcription is offered if the helper and a model are in place when the
     * engine is made (see asr_helper.h); a model added later is noticed by
     * RescanTranscription(), or at the next start of the program. */
    WasapiEngine();

    bool IsSimulated() const override { return false; }
    std::vector<DeviceInfo> ListDevices(DeviceKind kind) override;
    bool Start(const RecordingSettings &settings, const std::wstring &outputPath, std::wstring *error) override;
    void Stop() override;
    bool IsRecording() override { return session_ && session_->Running(); }
    RecordingStats Stats() override { return session_ ? session_->Stats() : RecordingStats(); }
    std::size_t TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity) override;
    std::wstring Failure() override { return session_ ? session_->Failure() : std::wstring(); }
    std::vector<std::wstring> OutputFiles() override { return session_ ? session_->Files() : std::vector<std::wstring>(); }

    bool CanTranscribe() const override { return transcribes_; }
    std::vector<TranscriptModel> TranscriptModels() const override { return models_; }
    void RescanTranscription() override;
    bool CanTranscribeOnGpu() const override { return transcribes_ && asr_.gpu; }
    TranscriptStatus Transcription(StreamId stream) override
    {
        return host_ ? host_->Status(stream) : TranscriptStatus();
    }
    std::wstring TranscriptionProblem(StreamId stream) override
    {
        return host_ ? host_->Problem(stream) : std::wstring();
    }
    std::size_t TakeTranscript(StreamId stream, TranscriptEvent *out, std::size_t capacity) override
    {
        return host_ ? host_->TakeEvents(stream, out, capacity) : 0;
    }
    std::vector<std::wstring> TranscriptFiles() override;
    std::wstring TranscriptFileProblem() override;

    bool CanEnhanceVoice() const override { return true; }
    EnhanceStatus Enhancement() override { return enhance_ ? enhance_->Status() : EnhanceStatus(); }
    void CancelEnhancement() override
    {
        if (enhance_)
            enhance_->Cancel();
    }
    std::vector<std::wstring> EnhancedFiles() override
    {
        return enhance_ ? enhance_->Done() : std::vector<std::wstring>();
    }

private:
    void BeginEnhancement();

    AsrSetup                         asr_;              /* the helper, with the first model */
    bool                             transcribes_ = false;
    std::vector<TranscriptModel>     models_;           /* the installed ones, the first one first */
    StopSignal                       stopSignal_;       /* exists only while recording */
    std::unique_ptr<TranscriptFile>  transcriptFiles_[kStreamCount];    /* of the streams transcribed; before host_, which writes to them */
    std::unique_ptr<TranscriptionHost> host_;           /* the current or last recording's transcriber, if it had one */
    std::unique_ptr<RecordingSession> session_;         /* the current or last recording */
    RecordingSettings                run_;              /* its settings */
    bool                             enhancePending_ = false;   /* the recording is to be followed by enhancement_, once it has stopped */
    std::unique_ptr<VoiceEnhanceJob> enhance_;          /* the last recording's clearer copies, being made or made */
};

/* Creates the folder `filePath` is to go in if it is missing, one level deep:
 * its parent must exist.  The default folder ("Documents\Audior-GUI") then needs no
 * set-up, and a typo further up the path is still reported rather than
 * repaired. */
void CreateOutputFolder(const std::wstring &filePath);

/* Linear gain for a percentage: 0 = unchanged, 100 = twice as loud, 300 = four
 * times. */
inline float GainForPercent(int percent) { return 1.0f + static_cast<float>(percent) / 100.0f; }

}  // namespace audiorgui
