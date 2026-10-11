#include <windows.h>

#include "../src/preferences.h"
#include "test_registry_key.h"
#include "test_support.h"

using namespace audiorgui;

TEST(Preferences_NothingStoredMeansTheDefaults)
{
    TestKey key;
    const Preferences prefs(key.Name());
    CHECK(prefs.LoadHotkey(HotkeyAction::Start) == DefaultHotkey(HotkeyAction::Start));
    CHECK(prefs.LoadHotkey(HotkeyAction::Stop) == DefaultHotkey(HotkeyAction::Stop));
}

TEST(Preferences_HotkeysComeBackAsSaved)
{
    TestKey key;
    const Hotkey start = {kModCtrl | kModAlt, 'M'}, stop = {0, 0x7A};             /* Ctrl+Alt+M, F11 */
    {
        const Preferences prefs(key.Name());
        CHECK(prefs.SaveHotkey(HotkeyAction::Start, start));
        CHECK(prefs.SaveHotkey(HotkeyAction::Stop, stop));
    }
    const Preferences later(key.Name());                /* a new run */
    CHECK(later.LoadHotkey(HotkeyAction::Start) == start);
    CHECK(later.LoadHotkey(HotkeyAction::Stop) == stop);
}

TEST(Preferences_TheTwoHotkeysAreKeptApart)
{
    TestKey key;
    const Preferences prefs(key.Name());
    CHECK(prefs.SaveHotkey(HotkeyAction::Start, {kModAlt, '1'}));
    CHECK(prefs.LoadHotkey(HotkeyAction::Stop) == DefaultHotkey(HotkeyAction::Stop));
    CHECK(prefs.SaveHotkey(HotkeyAction::Start, {kModAlt, '2'}));                  /* replaces, does not add */
    CHECK(prefs.LoadHotkey(HotkeyAction::Start) == (Hotkey{kModAlt, '2'}));
}

TEST(Preferences_AnInvalidHotkeyIsNotSaved)
{
    TestKey key;
    const Preferences prefs(key.Name());
    CHECK(prefs.SaveHotkey(HotkeyAction::Start, {kModAlt, 'Q'}));
    CHECK(!prefs.SaveHotkey(HotkeyAction::Start, {0, 'Q'}));                       /* a plain letter */
    CHECK(!prefs.SaveHotkey(HotkeyAction::Start, {kModCtrl, 0}));
    CHECK(prefs.LoadHotkey(HotkeyAction::Start) == (Hotkey{kModAlt, 'Q'}));
}

TEST(Preferences_AnythingUnreadableMeansTheDefault)
{
    TestKey key;
    const Preferences prefs(key.Name());
    key.SetText(L"StartHotkey", L"Ctrl+Banana");
    key.SetText(L"StopHotkey", L"S");                   /* valid text, but a plain letter */
    CHECK(prefs.LoadHotkey(HotkeyAction::Start) == DefaultHotkey(HotkeyAction::Start));
    CHECK(prefs.LoadHotkey(HotkeyAction::Stop) == DefaultHotkey(HotkeyAction::Stop));

    key.SetText(L"StartHotkey", L"");
    key.SetNumber(L"StopHotkey", 42);                   /* the wrong kind of value */
    CHECK(prefs.LoadHotkey(HotkeyAction::Start) == DefaultHotkey(HotkeyAction::Start));
    CHECK(prefs.LoadHotkey(HotkeyAction::Stop) == DefaultHotkey(HotkeyAction::Stop));

    key.SetText(L"StartHotkey", L"Ctrl+Alt+Shift+Insert+Ctrl+Alt+Shift+Insert+Ctrl+Alt+Shift+Insert");     /* too long to read */
    CHECK(prefs.LoadHotkey(HotkeyAction::Start) == DefaultHotkey(HotkeyAction::Start));
}

TEST(Preferences_ADeclinedDownloadIsRememberedUntilItIsTakenBack)
{
    TestKey key;
    const Preferences prefs(key.Name());
    CHECK(!prefs.LoadDownloadDeclined());               /* nothing stored: the question is still to be asked */

    CHECK(prefs.SaveDownloadDeclined(true));
    CHECK(Preferences(key.Name()).LoadDownloadDeclined());                         /* a new run */
    CHECK(prefs.LoadHotkey(HotkeyAction::Start) == DefaultHotkey(HotkeyAction::Start));     /* the hotkeys are not disturbed */

    CHECK(prefs.SaveDownloadDeclined(false));
    CHECK(!prefs.LoadDownloadDeclined());

    key.SetText(L"DownloadDeclined", L"1");             /* the wrong kind of value means "not declined" */
    CHECK(!prefs.LoadDownloadDeclined());
}
