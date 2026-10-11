#include "hotkey.h"

namespace audiorgui {

namespace {

constexpr unsigned kFirstFunctionKey = 0x70, kLastFunctionKey = 0x87;      /* F1 .. F24 */

struct NamedKey { unsigned key; const wchar_t *name; };
const NamedKey kNamedKeys[] = {
    {0x20, L"Space"}, {0x21, L"PgUp"}, {0x22, L"PgDn"}, {0x23, L"End"},   {0x24, L"Home"},   {0x25, L"Left"},
    {0x26, L"Up"},    {0x27, L"Right"}, {0x28, L"Down"}, {0x2D, L"Insert"}, {0x2E, L"Delete"},
};

bool IsLetterOrDigit(unsigned key) { return (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z'); }
bool IsFunctionKey(unsigned key) { return key >= kFirstFunctionKey && key <= kLastFunctionKey; }

wchar_t Upper(wchar_t c) { return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - L'a' + L'A') : c; }

bool EqualsIgnoringCase(const std::wstring &a, const wchar_t *b)
{
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        if (Upper(a[i]) != Upper(b[i]))
            return false;
    }
    return i == a.size() && !b[i];
}

std::wstring Trimmed(const std::wstring &text)
{
    const std::size_t first = text.find_first_not_of(L' ');
    return first == std::wstring::npos ? std::wstring()
                                       : text.substr(first, text.find_last_not_of(L' ') - first + 1);
}

/* The key named by `name`, or 0. */
unsigned KeyFromName(const std::wstring &name)
{
    if (name.size() == 1 && IsLetterOrDigit(static_cast<unsigned>(Upper(name[0]))))
        return static_cast<unsigned>(Upper(name[0]));
    if (name.size() >= 2 && name.size() <= 3 && Upper(name[0]) == L'F') {      /* "F1" .. "F24" */
        unsigned n = 0;
        for (std::size_t i = 1; i < name.size(); ++i) {
            if (name[i] < L'0' || name[i] > L'9')
                return 0;
            n = n * 10 + static_cast<unsigned>(name[i] - L'0');
        }
        if (n >= 1 && n <= 24)
            return kFirstFunctionKey + n - 1;
        return 0;
    }
    for (const NamedKey &k : kNamedKeys) {
        if (EqualsIgnoringCase(name, k.name))
            return k.key;
    }
    return 0;
}

}  // namespace

bool IsHotkeyKey(unsigned key)
{
    if (IsLetterOrDigit(key) || IsFunctionKey(key))
        return true;
    for (const NamedKey &k : kNamedKeys) {
        if (k.key == key)
            return true;
    }
    return false;
}

bool IsValidHotkey(const Hotkey &hotkey)
{
    if (!IsHotkeyKey(hotkey.key) || (hotkey.modifiers & ~(kModAlt | kModCtrl | kModShift)) != 0)
        return false;
    return IsFunctionKey(hotkey.key) || (hotkey.modifiers & (kModCtrl | kModAlt)) != 0;
}

std::wstring HotkeyProblem(const Hotkey &hotkey)
{
    if (!IsHotkeyKey(hotkey.key))
        return L"Use a letter, digit or function key.";
    if (!IsValidHotkey(hotkey))
        return L"Add Ctrl or Alt: a bare key would break typing.";
    return std::wstring();
}

std::wstring HotkeyProblem(HotkeyAction action, const Hotkey &hotkey, const Hotkey &other)
{
    std::wstring problem = HotkeyProblem(hotkey);
    if (problem.empty() && hotkey == other) {
        problem = FormatHotkey(hotkey) +
                  (action == HotkeyAction::Start ? L" is already the Stop shortcut." : L" is already the Start shortcut.");
    }
    return problem;
}

std::wstring FormatHotkey(const Hotkey &hotkey)
{
    std::wstring name;
    if (IsLetterOrDigit(hotkey.key)) {
        name = std::wstring(1, static_cast<wchar_t>(hotkey.key));
    } else if (IsFunctionKey(hotkey.key)) {
        name = L"F" + std::to_wstring(hotkey.key - kFirstFunctionKey + 1);
    } else {
        for (const NamedKey &k : kNamedKeys) {
            if (k.key == hotkey.key)
                name = k.name;
        }
    }
    if (name.empty())
        return name;

    std::wstring text;
    if (hotkey.modifiers & kModCtrl)
        text += L"Ctrl+";
    if (hotkey.modifiers & kModAlt)
        text += L"Alt+";
    if (hotkey.modifiers & kModShift)
        text += L"Shift+";
    return text + name;
}

bool ParseHotkey(const std::wstring &text, Hotkey *hotkey)
{
    Hotkey parsed;
    std::size_t start = 0;
    for (;;) {
        const std::size_t plus = text.find(L'+', start);
        const std::wstring part = Trimmed(text.substr(start, plus == std::wstring::npos ? plus : plus - start));
        if (plus == std::wstring::npos) {                       /* the last part is the key */
            parsed.key = KeyFromName(part);
            break;
        }
        if (EqualsIgnoringCase(part, L"Ctrl"))
            parsed.modifiers |= kModCtrl;
        else if (EqualsIgnoringCase(part, L"Alt"))
            parsed.modifiers |= kModAlt;
        else if (EqualsIgnoringCase(part, L"Shift"))
            parsed.modifiers |= kModShift;
        else
            return false;
        start = plus + 1;
    }
    if (!IsValidHotkey(parsed))
        return false;
    *hotkey = parsed;
    return true;
}

}  // namespace audiorgui
