/*
 * hotkey.h - The two system-wide keyboard shortcuts: one starts a recording,
 * one stops it.
 *
 * A Hotkey is a Windows virtual-key code plus Ctrl / Alt / Shift.  The rules
 * for which combinations are allowed, and their text form ("Ctrl+Alt+R"), live
 * here with no Windows headers so they are unit-tested; the window registers
 * them with the operating system.
 */
#pragma once

#include <string>

namespace audiorgui {

/* The modifier bits.  Their values are those of Win32's MOD_ALT, MOD_CONTROL
 * and MOD_SHIFT, so the window can pass them to RegisterHotKey as they are. */
inline constexpr unsigned kModAlt = 0x1, kModCtrl = 0x2, kModShift = 0x4;

struct Hotkey {
    unsigned modifiers = 0;
    unsigned key       = 0;         /* a virtual-key code; 0 = none */

    bool operator==(const Hotkey &o) const { return modifiers == o.modifiers && key == o.key; }
    bool operator!=(const Hotkey &o) const { return !(*this == o); }
};

enum class HotkeyAction { Start, Stop };
inline constexpr int kHotkeyActionCount = 2;

inline constexpr int HotkeyIndex(HotkeyAction action) { return static_cast<int>(action); }

/* Ctrl+Alt+R and Ctrl+Alt+S: unlike Ctrl+R and Ctrl+S they are not taken from
 * "reload" and "save" in every other program. */
inline constexpr Hotkey kDefaultStartHotkey = {kModCtrl | kModAlt, 'R'};
inline constexpr Hotkey kDefaultStopHotkey  = {kModCtrl | kModAlt, 'S'};

inline constexpr Hotkey DefaultHotkey(HotkeyAction action)
{
    return action == HotkeyAction::Start ? kDefaultStartHotkey : kDefaultStopHotkey;
}

/* Letters, digits, F1 to F24, Space and the navigation keys: the keys a hotkey
 * may be made of (virtual-key codes). */
bool IsHotkeyKey(unsigned key);

/* Whether the combination may be a hotkey: a supported key that does not
 * swallow ordinary typing.  A letter, digit, Space or navigation key needs Ctrl
 * or Alt (Shift alone would take every capital letter away from every program);
 * a function key may stand alone. */
bool IsValidHotkey(const Hotkey &hotkey);

/* Why `hotkey` cannot be used, in words for the user; empty if it can. */
std::wstring HotkeyProblem(const Hotkey &hotkey);

/* The same for `hotkey` as the shortcut of `action`, when the other action
 * already has `other`: the two must differ. */
std::wstring HotkeyProblem(HotkeyAction action, const Hotkey &hotkey, const Hotkey &other);

/* "Ctrl+Alt+Shift+R", "F9", "Ctrl+Space": modifiers in that order, then the
 * key.  Empty for a hotkey without a key. */
std::wstring FormatHotkey(const Hotkey &hotkey);

/* The reverse of FormatHotkey (ignoring case and spaces around the plus signs);
 * false if the text is not a valid hotkey, leaving *hotkey alone. */
bool ParseHotkey(const std::wstring &text, Hotkey *hotkey);

}  // namespace audiorgui
