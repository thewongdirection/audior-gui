#include "preferences.h"

#include <windows.h>

namespace audiorgui {

namespace {

const wchar_t *ValueName(HotkeyAction action)
{
    return action == HotkeyAction::Start ? L"StartHotkey" : L"StopHotkey";
}

const wchar_t kDeclinedValue[] = L"DownloadDeclined";

}  // namespace

Hotkey Preferences::LoadHotkey(HotkeyAction action) const
{
    wchar_t text[64];                                   /* "Ctrl+Alt+Shift+Insert" is 21 characters */
    DWORD bytes = sizeof(text);
    Hotkey hotkey;
    if (RegGetValueW(HKEY_CURRENT_USER, subKey_, ValueName(action), RRF_RT_REG_SZ, nullptr, text, &bytes) ==
            ERROR_SUCCESS &&
        ParseHotkey(text, &hotkey))
        return hotkey;
    return DefaultHotkey(action);
}

bool Preferences::SaveHotkey(HotkeyAction action, const Hotkey &hotkey) const
{
    if (!IsValidHotkey(hotkey))
        return false;
    const std::wstring text = FormatHotkey(hotkey);
    const DWORD bytes = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
    return RegSetKeyValueW(HKEY_CURRENT_USER, subKey_, ValueName(action), REG_SZ, text.c_str(), bytes) ==
           ERROR_SUCCESS;
}

bool Preferences::LoadDownloadDeclined() const
{
    DWORD value = 0, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, subKey_, kDeclinedValue, RRF_RT_REG_DWORD, nullptr, &value, &bytes) ==
               ERROR_SUCCESS &&
           value != 0;
}

bool Preferences::SaveDownloadDeclined(bool declined) const
{
    const DWORD value = declined ? 1 : 0;
    return RegSetKeyValueW(HKEY_CURRENT_USER, subKey_, kDeclinedValue, REG_DWORD, &value, sizeof(value)) ==
           ERROR_SUCCESS;
}

}  // namespace audiorgui
