#include <windows.h>

#include "../src/preferences.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

/* A registry key of the test's own, deleted when the test is done. */
class TestKey {
public:
    TestKey()
    {
        key_ = L"Software\\Audior-GUI-tests\\" + std::to_wstring(GetCurrentProcessId());
        RegDeleteTreeW(HKEY_CURRENT_USER, key_.c_str());
    }
    ~TestKey() { RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Audior-GUI-tests"); }

    const wchar_t *Name() const { return key_.c_str(); }

    void SetText(const wchar_t *value, const wchar_t *text) const
    {
        RegSetKeyValueW(HKEY_CURRENT_USER, key_.c_str(), value, REG_SZ, text,
                        static_cast<DWORD>((wcslen(text) + 1) * sizeof(wchar_t)));
    }

    void SetNumber(const wchar_t *value, DWORD number) const
    {
        RegSetKeyValueW(HKEY_CURRENT_USER, key_.c_str(), value, REG_DWORD, &number, sizeof(number));
    }

private:
    std::wstring key_;
};

}  // namespace

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
