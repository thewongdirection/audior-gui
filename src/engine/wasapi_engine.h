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
#include "recording_session.h"
#include "stop_signal.h"

namespace audiorgui {

class WasapiEngine final : public IRecordingEngine {
public:
    WasapiEngine() = default;

    bool IsSimulated() const override { return false; }
    std::vector<DeviceInfo> ListDevices(DeviceKind kind) override;
    bool Start(const RecordingSettings &settings, const std::wstring &outputPath, std::wstring *error) override;
    void Stop() override;
    bool IsRecording() override { return session_ && session_->Running(); }
    RecordingStats Stats() override { return session_ ? session_->Stats() : RecordingStats(); }
    std::size_t TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity) override;
    std::wstring Failure() override { return session_ ? session_->Failure() : std::wstring(); }
    std::vector<std::wstring> OutputFiles() override { return session_ ? session_->Files() : std::vector<std::wstring>(); }

private:
    StopSignal                       stopSignal_;       /* exists only while recording */
    std::unique_ptr<RecordingSession> session_;         /* the current or last recording */
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
