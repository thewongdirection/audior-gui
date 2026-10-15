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
    std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"Audior-GUI-%04d%02d%02d-%02d%02d%02d",
                  t.year, t.month, t.day, t.hour, t.minute, t.second);
    return buf;
}

const wchar_t *ExtensionFor(Format format)
{
    return format == Format::Wav ? L".wav" : L".mp3";
}

bool IsAbsolutePath(const std::wstring &path)
{
    return (path.size() >= 2 && path[1] == L':') || (!path.empty() && IsSeparator(path[0]));
}

std::wstring JoinPath(const std::wstring &dir, const std::wstring &name)
{
    if (dir.empty() || IsSeparator(dir.back()))
        return dir + name;
    return dir + L'\\' + name;
}

namespace {

/* The extension starts at the last '.' after the last path separator. */
std::size_t ExtensionStart(const std::wstring &path)
{
    for (std::size_t i = path.size(); i-- > 0;) {
        if (IsSeparator(path[i]))
            break;
        if (path[i] == L'.')
            return i;
    }
    return std::wstring::npos;
}

}  // namespace

std::wstring SuffixedPath(const std::wstring &path, const std::wstring &suffix)
{
    const std::size_t dot = ExtensionStart(path);
    if (dot == std::wstring::npos)
        return path + suffix;
    return path.substr(0, dot) + suffix + path.substr(dot);
}

std::wstring PlannedTranscriptFile(const RecordingSettings &s, const std::wstring &outputPath, StreamId slot)
{
    const std::size_t dot = ExtensionStart(outputPath);
    const wchar_t *suffix = Merges(s) ? L" - merged.txt"
                            : slot == StreamId::Microphone ? L" - microphone.txt" : L" - system audio.txt";
    return (dot == std::wstring::npos ? outputPath : outputPath.substr(0, dot)) + suffix;
}

std::vector<std::wstring> PlannedTranscriptFiles(const RecordingSettings &s, const std::wstring &outputPath)
{
    std::vector<std::wstring> files;
    for (int i = 0; i < kStreamCount; ++i) {
        if (Transcribes(s, static_cast<StreamId>(i)))
            files.push_back(PlannedTranscriptFile(s, outputPath, static_cast<StreamId>(i)));
    }
    return files;
}

std::wstring EnhancedPath(const std::wstring &recordedFile) { return SuffixedPath(recordedFile, L" - enhanced"); }

std::vector<std::wstring> PlannedEnhancedFiles(const RecordingSettings &s, const std::wstring &outputPath)
{
    std::vector<std::wstring> files;
    if (s.enhanceVoice) {
        for (const std::wstring &file : PlannedOutputFiles(s, outputPath))
            files.push_back(EnhancedPath(file));
    }
    return files;
}

std::vector<std::wstring> PlannedOutputFiles(const RecordingSettings &s,
                                             const std::wstring &outputPath)
{
    if (RecordsBoth(s) && s.mixMode == MixMode::Separate)
        return {SuffixedPath(outputPath, L"_mic"), SuffixedPath(outputPath, L"_system")};
    return {outputPath};
}

}  // namespace audiorgui
