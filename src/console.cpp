#include "console.h"

#include <windows.h>

#include <string>

namespace audiorgui {

namespace {

/* Where output for `which` (STD_OUTPUT_HANDLE or STD_ERROR_HANDLE) goes, or null. */
HANDLE Target(DWORD which)
{
    HANDLE handle = GetStdHandle(which);
    if (handle && handle != INVALID_HANDLE_VALUE)               /* redirected by whoever started us */
        return handle;

    /* Otherwise borrow the console of the process that started us, if it has one. */
    static const HANDLE console = [] {
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
            return static_cast<HANDLE>(nullptr);
        HANDLE out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        return out == INVALID_HANDLE_VALUE ? static_cast<HANDLE>(nullptr) : out;
    }();
    return console;
}

bool WriteText(HANDLE handle, const std::wstring &text)
{
    DWORD written = 0;
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode))
        return WriteConsoleW(handle, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr) != 0;

    /* A file or a pipe: UTF-8. */
    const int chars = static_cast<int>(text.size());
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), chars, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return false;
    std::string utf8(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), chars, &utf8[0], bytes, nullptr, nullptr);
    return WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) != 0;
}

void Write(DWORD which, const std::wstring &text, UINT icon)
{
    HANDLE target = Target(which);
    if (!target || !WriteText(target, text))
        MessageBoxW(nullptr, text.c_str(), L"Audior-GUI", MB_OK | icon);
}

}  // namespace

void WriteOut(const std::wstring &text) { Write(STD_OUTPUT_HANDLE, text, MB_ICONINFORMATION); }
void WriteError(const std::wstring &text) { Write(STD_ERROR_HANDLE, text, MB_ICONERROR); }

}  // namespace audiorgui
