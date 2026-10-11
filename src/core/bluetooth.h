/*
 * bluetooth.h - What the program knows about Bluetooth audio devices.
 *
 * A Bluetooth headset plays music in its stereo mode.  The moment anything opens its
 * microphone, Windows switches it to the hands-free (phone call) mode: the output drops out
 * for about half a second, its volume steps to the call mode's setting and the sound gets
 * thinner, and when the microphone is closed again it switches back.  Measured on real
 * hardware in docs/HARDWARE-TESTS.md.  Opening only the system audio (a loopback of the
 * headset's playback side) does none of that.
 *
 * Windows and the headset decide this, not the program, and nothing here can prevent it while
 * the microphone is open, so the window says so.  Plain C++, no Windows headers.
 */
#pragma once

#include <string>
#include <vector>

#include "types.h"

namespace audiorgui {

/* Windows names the bus a device hangs off: BTHENUM and BTHHFENUM (BTHLE... for LE Audio) for
 * Bluetooth, HDAUDIO, USB and so on for the rest. */
inline bool IsBluetoothEnumerator(const std::wstring &name)
{
    return name.size() >= 3 && (name[0] | 0x20) == L'b' && (name[1] | 0x20) == L't' && (name[2] | 0x20) == L'h';
}

/* Whether the microphone a recording would use is a Bluetooth one: the device with this id, or
 * the system default if `microphoneId` is empty. */
inline bool MicrophoneIsBluetooth(const std::vector<DeviceInfo> &microphones, const std::wstring &microphoneId)
{
    for (const DeviceInfo &d : microphones) {
        if (microphoneId.empty() ? d.isDefault : d.id == microphoneId)
            return d.bluetooth;
    }
    return false;
}

inline const wchar_t *BluetoothMicrophoneNote()
{
    return L"Bluetooth microphone: the headset switches to call mode while recording.";
}

}  // namespace audiorgui
