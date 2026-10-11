/*
 * test_registry_key.h - A registry key of a test's own, for tests of what the program remembers.
 *
 * The key is made fresh and deleted, with everything under it, when the object goes out of scope, so a
 * test run leaves nothing in the registry.
 */
#pragma once

#include <windows.h>

#include <string>

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

