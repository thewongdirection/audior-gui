/*
 * saved_settings.h - What the program remembers between runs, as text.
 *
 * The settings of the last run (every recording setting, and the two hotkeys)
 * are kept in a small text file of  key=value  lines; preferences.h says where
 * and when it is written.  It is meant to be read by a person too:
 *
 *     # Audior-GUI: the settings of the last run ...
 *     folder=C:\Users\Sam\Documents\Audior-GUI
 *     format=mp3
 *     microphone=on
 *     startHotkey=Ctrl+Alt+R
 *
 * Reading is forgiving, because the file may have been edited, damaged or
 * written by another version: a line is applied only if its key is known and
 * its value is valid, and every other line (unknown keys, bad values, comments,
 * blank lines) is skipped without touching the rest.  What the file does not
 * say validly stays as the caller had it, which is how a first run, a missing
 * file and a half-garbled one all read as the defaults.  Whatever is read is
 * also kept within bounds (lengths, ranges, absolute paths, one stream on, two
 * different hotkeys).
 *
 * Plain C++ with no Windows headers, so it is unit-tested.
 */
#pragma once

#include <cstddef>
#include <string>

#include "hotkey.h"
#include "settings.h"

namespace audiorgui {

/* A file longer than this is not one of ours (a real one is about 1 KB). */
inline constexpr std::size_t kMaxSavedSettingsBytes = 16 * 1024;

struct SavedSettings {
    RecordingSettings recording;
    Hotkey            hotkeys[kHotkeyActionCount] = {kDefaultStartHotkey, kDefaultStopHotkey};
    bool              downloadDeclined = false;     /* "Not now" to the offer to download a speech model: not asked again at start-up */
};

/* The file's text, lines ended by CR LF.  A new setting needs a line here and a
 * case in ParseSavedSettings; the round-trip test lists every field. */
std::wstring FormatSavedSettings(const SavedSettings &settings);

/* Applies the lines of `text` to *settings, which holds the defaults. */
void ParseSavedSettings(const std::wstring &text, SavedSettings *settings);

}  // namespace audiorgui
