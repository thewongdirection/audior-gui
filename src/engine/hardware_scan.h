/*
 * hardware_scan.h - Looks at this PC for what transcription could use.
 *
 * Asks Windows (DXGI, which ships with it) for the graphics adapters, leaving out
 * Windows' own software renderer, and checks whether the graphics driver has
 * installed a Vulkan loader (vulkan-1.dll), which the helper's GPU backend needs.
 * What the answer means is decided in core/setup_plan.
 */
#pragma once

#include "../core/setup_plan.h"

namespace audiorgui {

/* Takes a few milliseconds and loads nothing it leaves behind; may be called from any thread.  An empty
 * profile (no card, no driver) if Windows will not say. */
HardwareProfile ScanHardware();

}  // namespace audiorgui
