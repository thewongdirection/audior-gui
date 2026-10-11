/*
 * temp_dir.h - A scratch folder for tests that write files.
 *
 * Created under %TEMP% with a unique name and removed, with everything in it,
 * when the object goes out of scope, so a test run leaves nothing behind.
 */
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

class TempDir {
public:
    TempDir()
    {
        static int counter = 0;
        wchar_t base[MAX_PATH + 1];
        const DWORD n = GetTempPathW(MAX_PATH, base);
        path_ = std::wstring(base, n) + L"audior-gui-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(++counter);
        CreateDirectoryW(path_.c_str(), nullptr);
    }

    ~TempDir() { Remove(path_); }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    const std::wstring &Dir() const { return path_; }
    std::wstring Path(const std::wstring &name) const { return path_ + L"\\" + name; }

    /* The names of the files in the folder. */
    std::vector<std::wstring> Names() const
    {
        std::vector<std::wstring> names;
        WIN32_FIND_DATAW find;
        HANDLE h = FindFirstFileW((path_ + L"\\*").c_str(), &find);
        if (h == INVALID_HANDLE_VALUE)
            return names;
        do {
            if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                names.push_back(find.cFileName);
        } while (FindNextFileW(h, &find));
        FindClose(h);
        return names;
    }

private:
    /* Deletes `dir` and everything in it. */
    static void Remove(const std::wstring &dir)
    {
        WIN32_FIND_DATAW find;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &find);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = find.cFileName;
                if (name == L"." || name == L"..")
                    continue;
                if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    Remove(dir + L"\\" + name);
                else
                    DeleteFileW((dir + L"\\" + name).c_str());
            } while (FindNextFileW(h, &find));
            FindClose(h);
        }
        RemoveDirectoryW(dir.c_str());
    }

    std::wstring path_;
};

/* The whole content of a file; empty if it cannot be read. */
inline std::vector<std::uint8_t> ReadFileBytes(const std::wstring &path)
{
    std::vector<std::uint8_t> bytes;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return bytes;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 28)) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr))
            got = 0;
        bytes.resize(got);
    }
    CloseHandle(h);
    return bytes;
}
