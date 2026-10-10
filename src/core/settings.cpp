#include "settings.h"

#include <cwchar>

namespace audiorgui {

namespace {

bool IsSeparator(wchar_t c) { return c == L'\\' || c == L'/'; }

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
    if (s.mergeMicPercent < 0 || s.mergeMicPercent > 100)
        return L"The microphone's share of the mix must be between 0 and 100%.";
    return std::wstring();
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
