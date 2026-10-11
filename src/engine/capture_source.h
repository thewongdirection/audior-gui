/*
 * capture_source.h - One stream's audio, as the recording session sees it.
 *
 * The real implementation reads a WASAPI endpoint (microphone or the loopback
 * of a playback device).  Tests substitute generated audio, so the whole
 * recording pipeline can be exercised without any hardware.
 *
 * Every call comes from the stream's own capture thread, and the object is
 * released on that thread too (Stop() lets go of everything).
 */
#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

namespace audiorgui {

class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;

    /* Opens the device.  A failure here ends the Start() that asked for it. */
    virtual HRESULT Open() = 0;

    /* Starts capturing.  May take a while (a Bluetooth microphone needs half a
     * second or more to switch profile), during which the other streams'
     * devices are already running. */
    virtual HRESULT Start() = 0;

    /* The recording starts now, every device being up: forget the audio
     * captured so far, so that all the streams begin at the same moment. */
    virtual void Begin() {}

    /* Appends the audio captured since the last call as canonical frames
     * (48 kHz, 16-bit, stereo, after any gain).  Must not block for long.  A
     * failure means the device is gone; the audio appended before it is kept. */
    virtual HRESULT Poll(std::vector<std::int16_t> *frames) = 0;

    /* Stops the device and releases it. */
    virtual void Stop() = 0;
};

}  // namespace audiorgui
