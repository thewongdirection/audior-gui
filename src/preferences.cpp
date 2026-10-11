#include "preferences.h"

#include <windows.h>

#include <cstring>

#include "engine/asr_helper.h"

namespace audiorgui {

namespace {

const wchar_t kFileName[] = L"Audior-GUI.ini";
const char    kUtf8Bom[]  = "\xEF\xBB\xBF";                 /* Notepad can add it when a person saves the file */
const char    kUtf16Bom[] = "\xFF\xFE";                     /* PowerShell's `>` and Notepad's "Unicode" write UTF-16 */

/* The bytes of the file; false if it is missing, unreadable or longer than a settings file can be. */
bool ReadBytes(const std::wstring &path, std::string *bytes)
{
    /* Whoever else has the file open (an editor) or replaces it (a second copy saving) is no obstacle. */
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size = {};
    DWORD read = 0;
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart <= static_cast<LONGLONG>(kMaxSavedSettingsBytes);
    if (ok) {
        bytes->resize(static_cast<std::size_t>(size.QuadPart));
        ok = ReadFile(file, bytes->data(), static_cast<DWORD>(bytes->size()), &read, nullptr) && read == bytes->size();
    }
    CloseHandle(file);
    return ok;
}

/* Writes `bytes` to a new file at `path` and makes sure they reached the disk. */
bool WriteBytes(const std::wstring &path, const std::string &bytes)
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                    written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

std::wstring Widen(UINT codePage, DWORD flags, const char *bytes, int length)
{
    const int chars = MultiByteToWideChar(codePage, flags, bytes, length, nullptr, 0);
    if (chars <= 0)
        return std::wstring();
    std::wstring text(static_cast<std::size_t>(chars), L'\0');
    MultiByteToWideChar(codePage, flags, bytes, length, text.data(), chars);
    return text;
}

/* The text of the file: UTF-16 if it starts with that byte order mark, else UTF-8 (a UTF-8 mark is skipped),
 * else, if it is not valid UTF-8, in the system code page (Notepad's "ANSI"). */
std::wstring ToText(const std::string &bytes)
{
    if (bytes.compare(0, 2, kUtf16Bom) == 0) {
        std::wstring text((bytes.size() - 2) / sizeof(wchar_t), L'\0');
        std::memcpy(text.data(), bytes.data() + 2, text.size() * sizeof(wchar_t));
        return text;
    }
    const std::size_t skip = bytes.compare(0, 3, kUtf8Bom) == 0 ? 3 : 0;
    const char *data = bytes.data() + skip;
    const int length = static_cast<int>(bytes.size() - skip);
    if (length == 0)
        return std::wstring();
    const std::wstring utf8 = Widen(CP_UTF8, MB_ERR_INVALID_CHARS, data, length);
    return utf8.empty() ? Widen(CP_ACP, 0, data, length) : utf8;
}

/* Text to UTF-8; empty if it cannot be (a lone surrogate). */
std::string ToBytes(const std::wstring &text)
{
    const int length = static_cast<int>(text.size());
    const int bytes =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return std::string();
    std::string utf8(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, utf8.data(), bytes, nullptr, nullptr);
    return utf8;
}

bool IsDirectory(const std::wstring &path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/* Whether a recording can go to `folder`: it is there, or the folder above it is, as a recording makes the last
 * folder of its path (see CreateOutputFolder).  A folder from a settings file that was copied from another PC,
 * or that is on a drive since removed, is not. */
bool CanUseFolder(const std::wstring &folder)
{
    if (IsDirectory(folder))
        return true;
    const std::size_t slash = folder.find_last_of(L"\\/");
    return slash != std::wstring::npos && IsDirectory(folder.substr(0, slash + 1));
}

}  // namespace

std::wstring Preferences::DefaultPath()
{
    const std::wstring directory = ProgramDirectory();
    return directory.empty() ? std::wstring() : directory + L'\\' + kFileName;
}

void Preferences::Load(SavedSettings *settings) const
{
    std::string bytes;
    if (path_.empty() || !ReadBytes(path_, &bytes))
        return;
    const std::wstring defaultFolder = settings->recording.outputDirectory;
    ParseSavedSettings(ToText(bytes), settings);
    if (!CanUseFolder(settings->recording.outputDirectory))
        settings->recording.outputDirectory = defaultFolder;
}

bool Preferences::Save(const SavedSettings &settings) const
{
    const std::string bytes = ToBytes(FormatSavedSettings(settings));
    if (path_.empty() || bytes.empty())
        return false;
    const std::wstring temporary = path_ + L".tmp";
    const bool ok = WriteBytes(temporary, bytes) &&
                    MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok)
        DeleteFileW(temporary.c_str());
    return ok;
}

}  // namespace audiorgui
