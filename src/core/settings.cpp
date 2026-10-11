#include "settings.h"

#include <cwchar>

#include "asr_models.h"

namespace audiorgui {

namespace {

bool IsSeparator(wchar_t c) { return c == L'\\' || c == L'/'; }

wchar_t AsciiUpper(wchar_t c) { return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - L'a' + L'A') : c; }

}  // namespace

bool IsValidMp3Bitrate(int kbps)
{
    for (int rate : kMp3Bitrates) {
        if (rate == kbps)
            return true;
    }
    return false;
}

std::wstring Validate(const RecordingSettings &s)
{
    if (!s.includeMicrophone && !s.includeSystemAudio)
        return L"Turn on at least one stream: the microphone or system audio.";
    if (s.outputDirectory.empty())
        return L"Choose a folder to save recordings in.";
    if (s.format == Format::Mp3 && !IsValidMp3Bitrate(s.mp3BitrateKbps))
        return L"Choose a supported MP3 bitrate (96 to 320 kbps).";
    if (s.micAmplifyPercent < 0 || s.micAmplifyPercent > kMaxAmplifyPercent)
        return L"Microphone gain must be between 0 and 300%.";
    if (s.systemAmplifyPercent < 0 || s.systemAmplifyPercent > kMaxAmplifyPercent)
        return L"System audio gain must be between 0 and 300%.";
    if (s.mergeMicPercent < 0 || s.mergeMicPercent > 100)
        return L"The microphone's share of the mix must be between 0 and 100%.";
    if (!s.transcribeModel.empty() && !FindAsrModel(s.transcribeModel))
        return L"The transcription model \"" + s.transcribeModel + L"\" is not one this program knows.";
    return FileNameProblem(s.fileName);
}

bool IsFileNameChar(wchar_t c)
{
    return c >= 32 && std::wcschr(L"<>:\"/\\|?*", c) == nullptr;
}

std::wstring FileNameProblem(const std::wstring &name)
{
    if (name.empty())
        return std::wstring();
    for (wchar_t c : name) {
        if (!IsFileNameChar(c))
            return L"The file name cannot contain control characters or any of  < > : \" / \\ | ? *";
    }

    /* Nothing left once the extension is taken off: ".mp3", "...", " ". */
    if (name.substr(0, name.rfind(L'.')).find_first_not_of(L". ") == std::wstring::npos)
        return L"Choose a file name.";

    /* Windows reserves the device names whatever the extension ("CON.mp3"). */
    std::wstring stem = name.substr(0, name.find(L'.'));
    while (!stem.empty() && stem.back() == L' ')
        stem.pop_back();
    for (wchar_t &c : stem)
        c = AsciiUpper(c);
    bool reserved = stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL";
    if (stem.size() == 4 && (stem.compare(0, 3, L"COM") == 0 || stem.compare(0, 3, L"LPT") == 0))
        reserved = reserved || (stem[3] >= L'1' && stem[3] <= L'9');
    return reserved ? std::wstring(L"That file name is reserved by Windows. Choose another.") : std::wstring();
}

std::wstring EnforceExtension(const std::wstring &name, Format format)
{
    const std::wstring ext = ExtensionFor(format);
    const std::size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos && name.size() - dot == ext.size()) {
        bool same = true;
        for (std::size_t i = 0; i < ext.size(); ++i)
            same = same && AsciiUpper(name[dot + i]) == AsciiUpper(ext[i]);
        if (same)
            return name;
    }
    return name.substr(0, dot) + ext;               /* no dot: the whole name */
}

std::wstring DefaultBaseName(const LocalTime &t)
{
    wchar_t buf[40];
    std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"audior-%04d%02d%02d-%02d%02d%02d",
                  t.year, t.month, t.day, t.hour, t.minute, t.second);
    return buf;
}

const wchar_t *ExtensionFor(Format format)
{
    return format == Format::Wav ? L".wav" : L".mp3";
}

std::wstring JoinPath(const std::wstring &dir, const std::wstring &name)
{
    if (dir.empty() || IsSeparator(dir.back()))
        return dir + name;
    return dir + L'\\' + name;
}

std::wstring SuffixedPath(const std::wstring &path, const std::wstring &suffix)
{
    /* The extension starts at the last '.' after the last path separator. */
    std::size_t dot = std::wstring::npos;
    for (std::size_t i = path.size(); i-- > 0;) {
        if (IsSeparator(path[i]))
            break;
        if (path[i] == L'.') {
            dot = i;
            break;
        }
    }
    if (dot == std::wstring::npos)
        return path + suffix;
    return path.substr(0, dot) + suffix + path.substr(dot);
}

std::vector<std::wstring> PlannedOutputFiles(const RecordingSettings &s,
                                             const std::wstring &outputPath)
{
    if (RecordsBoth(s) && s.mixMode == MixMode::Separate)
        return {SuffixedPath(outputPath, L"_mic"), SuffixedPath(outputPath, L"_system")};
    return {outputPath};
}

}  // namespace audiorgui
