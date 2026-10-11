/*
 * devices.h - The audio endpoints Windows reports.
 */
#pragma once

#include <vector>

#include "../core/types.h"

namespace audiorgui {

/* The active microphones (Microphone) or playback devices (Playback), in the
 * order Windows lists them, with the system default flagged.  Needs COM on the
 * calling thread; returns an empty list if the audio service cannot be reached. */
std::vector<DeviceInfo> EnumerateDevices(DeviceKind kind);

}  // namespace audiorgui
