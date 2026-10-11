/*
 * screenshot.h - Renders the window's content to a PNG without showing a window.
 *
 *     Audior-GUI.exe --screenshot out.png [--scenario recording] [--size 1120x720]
 *
 * Uses the same MainView and painter as the real window, over a WIC bitmap
 * with Direct2D's software renderer, driven by a simulated engine on a manual
 * clock, so the picture is identical on every machine.  CI uses it to publish
 * a picture of the UI, and it is a quick way to look at a design change.
 *
 * Scenarios: idle, recording, mic-only, system-only, bluetooth-mic (a headset microphone chosen), clipping,
 * saved, error, named (settings changed), typing (a long file name being edited), hotkeys (a shortcut box
 * waiting for keys), hotkey-taken (a hotkey another program already owns).
 */
#pragma once

#include <string>

#include "gfx.h"

namespace audiorgui::ui {

struct ScreenshotOptions {
    std::wstring path;
    std::wstring scenario = L"recording";
    UINT         width    = 1120;
    UINT         height   = 720;
};

/* Returns 0 on success, 2 for an unknown scenario, 1 for any other failure. */
int RunScreenshot(const Gfx &gfx, const ScreenshotOptions &options);

}  // namespace audiorgui::ui
