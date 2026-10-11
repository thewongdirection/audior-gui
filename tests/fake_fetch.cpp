#include "fake_fetch.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "../src/core/fetch_protocol.h"

namespace {

constexpr std::size_t kChunk = 16 * 1024;

void Say(const std::string &line)
{
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

std::wstring Wide(const char *text)                     /* argv arrives in the ANSI code page */
{
    const int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    std::wstring wide(n > 0 ? static_cast<std::size_t>(n) - 1 : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_ACP, 0, text, -1, &wide[0], n);
    return wide;
}

/* Writes `content` to `file`, saying how far it has got after each piece when `report`. */
bool Write(const std::wstring &file, const std::string &content, bool report)
{
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    for (std::size_t at = 0; at < content.size(); at += kChunk) {
        const DWORD n = static_cast<DWORD>(std::min(kChunk, content.size() - at));
        DWORD written = 0;
        if (!WriteFile(h, content.data() + at, n, &written, nullptr))
            break;
        if (report)
            Say(audiorgui::fetch::ProgressLine(at + n));
    }
    CloseHandle(h);
    return true;
}

}  // namespace

std::string FakeFetchContent(std::size_t bytes)
{
    std::string content(bytes, '\0');
    for (std::size_t i = 0; i < bytes; ++i)
        content[i] = static_cast<char>((i * 31 + 7) & 0xFF);
    return content;
}

int RunFakeFetch(const char *mode, const char *url, const char *file, const char *bytes)
{
    const std::string m = mode, address = url;
    const std::wstring path = Wide(file);
    const std::size_t size = static_cast<std::size_t>(std::strtoull(bytes, nullptr, 10));

    if (m == "ok") {                                    /* only what the installer promises to give */
        const std::size_t slash = path.find_last_of(L'\\');
        const std::wstring name = path.substr(slash + 1);
        const std::wstring expected = L"/resolve/main/" + name.substr(0, name.size() - 5);
        const std::wstring wideUrl = Wide(url);
        const bool good = address.rfind("https://huggingface.co/", 0) == 0 && name.size() > 5 &&
                          name.compare(name.size() - 5, 5, L".part") == 0 && size > 0 &&
                          wideUrl.size() > expected.size() &&
                          wideUrl.compare(wideUrl.size() - expected.size(), expected.size(), expected) == 0;
        if (!good) {
            Say(audiorgui::fetch::ErrorLine("the program was not given what it should be given"));
            return 1;
        }
    }

    if (m == "ok" || m == "short" || m == "corrupt") {
        std::string content = FakeFetchContent(size);
        if (m == "short")
            content.pop_back();
        if (m == "corrupt")
            content[0] = static_cast<char>(content[0] ^ 0x55);
        if (!Write(path, content, true)) {
            Say(audiorgui::fetch::ErrorLine("could not create the file"));
            return 1;
        }
        Say(audiorgui::fetch::DoneLine());
        return 0;
    }

    Write(path, FakeFetchContent(std::min<std::size_t>(size, 100)), false);
    if (m == "error") {
        Say(audiorgui::fetch::ErrorLine("no route"));
        return 2;
    }
    if (m == "hang") {
        Say(audiorgui::fetch::ProgressLine(100));
        Sleep(120000);
        return 0;
    }
    return 5;                                           /* "die" */
}
