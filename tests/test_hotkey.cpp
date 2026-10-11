#include "../src/core/hotkey.h"
#include "test_support.h"

using namespace audiorgui;

TEST(Hotkey_DefaultsAreCtrlAltRToStartAndCtrlAltSToStop)
{
    CHECK(DefaultHotkey(HotkeyAction::Start) == (Hotkey{kModCtrl | kModAlt, 'R'}));
    CHECK(DefaultHotkey(HotkeyAction::Stop) == (Hotkey{kModCtrl | kModAlt, 'S'}));
    CHECK_EQ(FormatHotkey(DefaultHotkey(HotkeyAction::Start)), std::wstring(L"Ctrl+Alt+R"));
    CHECK_EQ(FormatHotkey(DefaultHotkey(HotkeyAction::Stop)), std::wstring(L"Ctrl+Alt+S"));
    CHECK(IsValidHotkey(DefaultHotkey(HotkeyAction::Start)));
    CHECK(IsValidHotkey(DefaultHotkey(HotkeyAction::Stop)));
    CHECK(DefaultHotkey(HotkeyAction::Start) != DefaultHotkey(HotkeyAction::Stop));
}

TEST(Hotkey_AKeyThatTypesNeedsCtrlOrAlt)
{
    CHECK(IsValidHotkey({kModCtrl, 'R'}));
    CHECK(IsValidHotkey({kModAlt, '1'}));
    CHECK(IsValidHotkey({kModCtrl | kModShift, 'R'}));
    CHECK(IsValidHotkey({kModCtrl | kModAlt | kModShift, 0x20}));       /* Space */

    CHECK(!IsValidHotkey({0, 'R'}));                    /* would take the letter from every program */
    CHECK(!IsValidHotkey({kModShift, 'R'}));            /* ... and so would a capital letter */
    CHECK(!IsValidHotkey({kModShift, 0x25}));
    CHECK(!HotkeyProblem({0, 'R'}).empty());
    CHECK(HotkeyProblem({kModCtrl, 'R'}).empty());
}

TEST(Hotkey_AFunctionKeyMayStandAlone)
{
    CHECK(IsValidHotkey({0, 0x70}));                    /* F1 */
    CHECK(IsValidHotkey({0, 0x87}));                    /* F24 */
    CHECK(IsValidHotkey({kModShift, 0x78}));            /* Shift+F9 */
    CHECK(!IsValidHotkey({0, 0x6F}));                   /* the key before F1 */
    CHECK(!IsValidHotkey({0, 0x88}));                   /* the key after F24 */
}

TEST(Hotkey_OnlyTheListedKeysAreAccepted)
{
    CHECK(!IsValidHotkey({kModCtrl, 0}));               /* no key */
    CHECK(!IsValidHotkey({kModCtrl, 0x09}));            /* Tab */
    CHECK(!IsValidHotkey({kModCtrl, 0x0D}));            /* Enter */
    CHECK(!IsValidHotkey({kModCtrl, 0x1B}));            /* Escape */
    CHECK(!IsValidHotkey({kModCtrl, 0x60}));            /* a numeric-keypad 0 */
    CHECK(!IsValidHotkey({kModCtrl, 0x5B}));            /* the left Windows key */
    CHECK(!IsValidHotkey({kModCtrl | 0x8, 'R'}));       /* the Windows key is not supported */
    CHECK(!HotkeyProblem({kModCtrl, 0x09}).empty());
}

TEST(Hotkey_StartAndStopMustDiffer)
{
    const Hotkey start = kDefaultStartHotkey, stop = kDefaultStopHotkey;
    CHECK(HotkeyProblem(HotkeyAction::Start, {kModAlt, 'Q'}, stop).empty());
    CHECK(HotkeyProblem(HotkeyAction::Stop, {kModAlt, 'Q'}, start).empty());
    CHECK(HotkeyProblem(HotkeyAction::Start, start, stop).empty());                /* keeping its own is fine */

    const std::wstring clash = HotkeyProblem(HotkeyAction::Start, stop, stop);
    CHECK_EQ(clash, std::wstring(L"Ctrl+Alt+S is already the Stop shortcut."));
    CHECK_EQ(HotkeyProblem(HotkeyAction::Stop, start, start), std::wstring(L"Ctrl+Alt+R is already the Start shortcut."));

    /* A shortcut that is unusable is reported as that, not as a clash. */
    CHECK_EQ(HotkeyProblem(HotkeyAction::Start, {0, 'S'}, {0, 'S'}), HotkeyProblem({0, 'S'}));
    CHECK(!HotkeyProblem(HotkeyAction::Start, {0, 'S'}, stop).empty());
}

TEST(Hotkey_FormatsModifiersThenTheKey)
{
    CHECK_EQ(FormatHotkey({kModCtrl, 'R'}), std::wstring(L"Ctrl+R"));
    CHECK_EQ(FormatHotkey({kModShift | kModAlt | kModCtrl, '7'}), std::wstring(L"Ctrl+Alt+Shift+7"));
    CHECK_EQ(FormatHotkey({0, 0x78}), std::wstring(L"F9"));
    CHECK_EQ(FormatHotkey({kModAlt, 0x87}), std::wstring(L"Alt+F24"));
    CHECK_EQ(FormatHotkey({kModCtrl, 0x20}), std::wstring(L"Ctrl+Space"));
    CHECK_EQ(FormatHotkey({kModCtrl, 0x21}), std::wstring(L"Ctrl+PgUp"));
    CHECK_EQ(FormatHotkey({kModCtrl, 0}), std::wstring());
}

TEST(Hotkey_ParsesWhatItFormats)
{
    /* Every valid key, with every combination of modifiers. */
    int checked = 0;
    for (unsigned key = 0; key < 0x100; ++key) {
        for (unsigned mods = 0; mods < 8; ++mods) {
            const Hotkey hotkey = {mods, key};
            if (!IsValidHotkey(hotkey))
                continue;
            Hotkey parsed;
            CHECK(ParseHotkey(FormatHotkey(hotkey), &parsed));
            CHECK(parsed == hotkey);
            ++checked;
        }
    }
    CHECK(checked > 200);
}

TEST(Hotkey_ParsingIgnoresCaseAndSpaces)
{
    Hotkey h;
    CHECK(ParseHotkey(L"ctrl + alt + r", &h));
    CHECK(h == (Hotkey{kModCtrl | kModAlt, 'R'}));
    CHECK(ParseHotkey(L"  CTRL+shift+f12 ", &h));
    CHECK(h == (Hotkey{kModCtrl | kModShift, 0x7B}));
    CHECK(ParseHotkey(L"ctrl+pgdn", &h));
    CHECK(h == (Hotkey{kModCtrl, 0x22}));
    CHECK(ParseHotkey(L"Ctrl+Alt+Ctrl+R", &h));          /* a repeated modifier is harmless */
    CHECK(h == (Hotkey{kModCtrl | kModAlt, 'R'}));
}

TEST(Hotkey_ParsingRejectsAnythingElseAndLeavesTheResultAlone)
{
    Hotkey h = {kModAlt, 'Q'};
    CHECK(!ParseHotkey(L"", &h));
    CHECK(!ParseHotkey(L"Ctrl", &h));                   /* no key */
    CHECK(!ParseHotkey(L"Ctrl+", &h));
    CHECK(!ParseHotkey(L"Ctrl++", &h));
    CHECK(!ParseHotkey(L"R", &h));                      /* needs Ctrl or Alt */
    CHECK(!ParseHotkey(L"Ctrl+Win+R", &h));             /* unknown modifier */
    CHECK(!ParseHotkey(L"Ctrl+RR", &h));                /* unknown key */
    CHECK(!ParseHotkey(L"Ctrl+F0", &h));
    CHECK(!ParseHotkey(L"Ctrl+F25", &h));
    CHECK(!ParseHotkey(L"Ctrl+F1x", &h));
    CHECK(!ParseHotkey(L"Ctrl+Tab", &h));
    CHECK(h == (Hotkey{kModAlt, 'Q'}));
}
