/*
 * preferences.h - What the program remembers between runs.
 *
 * Only the two hotkeys, and whether the user declined to download a speech
 * model, are kept: the recording settings (folder, format, devices...) are per
 * recording and start from audior's defaults, as the command line presets
 * them.  Values live per user in the registry
 * (HKEY_CURRENT_USER\Software\Audior-GUI), as text a person can read, and
 * anything missing or invalid reads as the default.
 */
#pragma once

#include "core/hotkey.h"

namespace audiorgui {

class Preferences {
public:
    /* `subKey` is where the values live under HKEY_CURRENT_USER; the tests
     * give it a key of their own. */
    explicit Preferences(const wchar_t *subKey = L"Software\\Audior-GUI") : subKey_(subKey) {}

    Hotkey LoadHotkey(HotkeyAction action) const;

    /* False if the registry refused or the hotkey is not valid. */
    bool SaveHotkey(HotkeyAction action, const Hotkey &hotkey) const;

    /* Whether the user answered "not now" to the offer to download a speech model, so that it is
     * not made again at every start. */
    bool LoadDownloadDeclined() const;
    bool SaveDownloadDeclined(bool declined) const;

private:
    const wchar_t *subKey_;
};

}  // namespace audiorgui
