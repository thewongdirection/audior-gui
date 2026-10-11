/*
 * wasapi_capture.h - Captures one WASAPI endpoint: a microphone, or the
 * loopback of a playback device ("what you hear").
 *
 * The device is opened in shared mode in its own mix format and converted to
 * the canonical format (see pcm.h) with the stream's gain applied.  The stream is
 * kept in step with the recording's clock, padded with silence while the device is
 * idle or still starting (see silence_filler.h and session_clock.h).
 *
 * All calls must come from one thread that has joined COM.
 */
#pragma once

#include <windows.h>

#include <mmsystem.h>
#include <mmreg.h>
#include <objbase.h>

#include <audioclient.h>
#include <wrl/client.h>

#include <memory>
#include <string>

#include "capture_source.h"
#include "pcm.h"
#include "session_clock.h"
#include "silence_filler.h"

namespace audiorgui {

class WasapiCapture final : public ICaptureSource {
public:
    /* `deviceId` is an endpoint ID, or empty for the system default.  `gain`
     * is linear (1 = unchanged).  The streams of one recording pass the same
     * `clock`; a stream on its own can leave it out. */
    WasapiCapture(std::wstring deviceId, bool loopback, float gain, std::shared_ptr<SessionClock> clock = nullptr);
    ~WasapiCapture() override { Stop(); }

    HRESULT Open() override;
    HRESULT Start() override;
    HRESULT Poll(std::vector<std::int16_t> *frames) override;
    void    Stop() override;

private:
    double SecondsRunning() const;

    std::wstring  deviceId_;
    bool          loopback_;
    float         gain_;

    Microsoft::WRL::ComPtr<IAudioClient>        client_;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> capture_;
    PcmConverter  converter_;
    SilenceFiller filler_;
    std::shared_ptr<SessionClock> clock_;
    bool          started_ = false;
    long long     originTicks_ = 0;             /* performance counter at the recording's start */
    long long     ticksPerSecond_ = 1;
};

/* Stops Windows from lowering other programs' volume while the microphone is
 * open (it treats a capture stream as a phone call and "ducks" everything else
 * by 80%).  Best effort: if it fails, ducking may happen.  Call it before the
 * streams are opened. */
void OptOutOfDucking();

}  // namespace audiorgui
