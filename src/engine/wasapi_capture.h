/*
 * wasapi_capture.h - Captures one WASAPI endpoint: a microphone, or the
 * loopback of a playback device ("what you hear").
 *
 * The device is opened in shared mode in its own mix format and converted to
 * the canonical format (see pcm.h) with the stream's gain applied.  A loopback
 * stream is padded with silence while the device is idle (see silence_filler.h).
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

#include <string>

#include "capture_source.h"
#include "pcm.h"
#include "silence_filler.h"

namespace audiorgui {

class WasapiCapture final : public ICaptureSource {
public:
    /* `deviceId` is an endpoint ID, or empty for the system default.  `gain`
     * is linear (1 = unchanged). */
    WasapiCapture(std::wstring deviceId, bool loopback, float gain);
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
    bool          started_ = false;
    long long     startTicks_ = 0;              /* performance counter at Start() */
    long long     ticksPerSecond_ = 1;
};

/* Stops Windows from lowering other programs' volume while the microphone is
 * open (it treats a capture stream as a phone call and "ducks" everything else
 * by 80%).  Best effort: if it fails, ducking may happen.  Call it before the
 * streams are opened. */
void OptOutOfDucking();

}  // namespace audiorgui
