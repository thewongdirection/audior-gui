#include "saved_settings.h"

#include "asr_models.h"
#include "format.h"

namespace audiorgui {

namespace {

const wchar_t kHeader[] =
    L"# Audior-GUI: the settings of the last run, read when the program starts.\r\n"
    L"# Change them here while the program is closed, or delete this file to start afresh.\r\n";

/* The longest device ID read back.  With the other limits this keeps a file we write well inside
 * kMaxSavedSettingsBytes, so that a file we wrote is never refused for its size. */
constexpr std::size_t kMaxIdChars = 512;

/* Control characters would end a line early (and have no place in a path or a name); a surrogate
 * that is not half of a pair cannot be written as UTF-8 and would fail the whole file. */
bool IsPlain(const std::wstring &text)
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (c < 32)
            return false;
        if (c >= 0xD800 && c <= 0xDBFF) {                   /* a high surrogate needs its low one */
            if (i + 1 >= text.size() || text[i + 1] < 0xDC00 || text[i + 1] > 0xDFFF)
                return false;
            ++i;
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            return false;
        }
    }
    return true;
}

const wchar_t *OnOff(bool on) { return on ? L"on" : L"off"; }

/* Adds "key=value"; a value that is not plain text is left out, so it reads back as "not said". */
void AddLine(std::wstring *text, const wchar_t *key, const std::wstring &value)
{
    if (IsPlain(value))
        *text += std::wstring(key) + L'=' + value + L"\r\n";
}

std::wstring Trimmed(const std::wstring &text)
{
    const std::size_t first = text.find_first_not_of(L" \t\r");
    return first == std::wstring::npos ? std::wstring()
                                       : text.substr(first, text.find_last_not_of(L" \t\r") - first + 1);
}

/* The readers below set *out only if `value` is valid. */

void ReadOnOff(const std::wstring &value, bool *out)
{
    if (value == L"on" || value == L"off")
        *out = value == L"on";
}

void ReadPercent(const std::wstring &value, int maximum, int *out)
{
    int percent = 0;
    if (ParseWholeNumber(value, &percent) && percent <= maximum)
        *out = percent;
}

void ReadText(const std::wstring &value, std::size_t maxChars, std::wstring *out)
{
    if (value.size() <= maxChars && IsPlain(value))
        *out = value;
}

void Apply(SavedSettings *saved, const std::wstring &key, const std::wstring &value)
{
    RecordingSettings &s = saved->recording;
    int kbps = 0;

    if (key == L"folder") {
        if (IsAbsolutePath(value))                          /* not one that depends on where the program was started */
            ReadText(value, kMaxOutputPathChars, &s.outputDirectory);
    } else if (key == L"fileName") {
        if (FileNameProblem(value).empty())
            ReadText(value, kMaxOutputPathChars, &s.fileName);
    } else if (key == L"format") {
        if (value == L"mp3" || value == L"wav")
            s.format = value == L"mp3" ? Format::Mp3 : Format::Wav;
    } else if (key == L"bitrate") {
        if (ParseWholeNumber(value, &kbps) && IsValidMp3Bitrate(kbps))
            s.mp3BitrateKbps = kbps;
    } else if (key == L"microphone") {
        ReadOnOff(value, &s.includeMicrophone);
    } else if (key == L"microphoneDevice") {
        ReadText(value, kMaxIdChars, &s.microphoneId);
    } else if (key == L"microphoneGain") {
        ReadPercent(value, kMaxAmplifyPercent, &s.micAmplifyPercent);
    } else if (key == L"systemAudio") {
        ReadOnOff(value, &s.includeSystemAudio);
    } else if (key == L"systemDevice") {
        ReadText(value, kMaxIdChars, &s.systemDeviceId);
    } else if (key == L"systemGain") {
        ReadPercent(value, kMaxAmplifyPercent, &s.systemAmplifyPercent);
    } else if (key == L"mix") {
        if (value == L"merge" || value == L"separate")
            s.mixMode = value == L"merge" ? MixMode::Merge : MixMode::Separate;
    } else if (key == L"mixMicrophone") {
        ReadPercent(value, 100, &s.mergeMicPercent);
    } else if (key == L"transcribeMicrophone") {
        ReadOnOff(value, &s.transcribeMicrophone);
    } else if (key == L"transcribeSystem") {
        ReadOnOff(value, &s.transcribeSystem);
    } else if (key == L"transcribeModel") {
        if (value.empty())
            s.transcribeModel.clear();
        else if (const AsrModel *model = FindAsrModel(value))
            s.transcribeModel = model->file;
    } else if (key == L"transcribeLanguage") {
        if (value == L"auto" || value == L"en")
            s.transcribeLanguage = value == L"en" ? SpeechLanguage::English : SpeechLanguage::Auto;
    } else if (key == L"transcribeGpu") {
        ReadOnOff(value, &s.transcribeGpu);
    } else if (key == L"startHotkey") {
        ParseHotkey(value, &saved->hotkeys[HotkeyIndex(HotkeyAction::Start)]);
    } else if (key == L"stopHotkey") {
        ParseHotkey(value, &saved->hotkeys[HotkeyIndex(HotkeyAction::Stop)]);
    }
}

}  // namespace

std::wstring FormatSavedSettings(const SavedSettings &saved)
{
    const RecordingSettings &s = saved.recording;
    std::wstring text = kHeader;
    AddLine(&text, L"folder", s.outputDirectory);
    AddLine(&text, L"fileName", s.fileName);
    AddLine(&text, L"format", s.format == Format::Mp3 ? L"mp3" : L"wav");
    AddLine(&text, L"bitrate", std::to_wstring(s.mp3BitrateKbps));
    AddLine(&text, L"microphone", OnOff(s.includeMicrophone));
    AddLine(&text, L"microphoneDevice", s.microphoneId);
    AddLine(&text, L"microphoneGain", std::to_wstring(s.micAmplifyPercent));
    AddLine(&text, L"systemAudio", OnOff(s.includeSystemAudio));
    AddLine(&text, L"systemDevice", s.systemDeviceId);
    AddLine(&text, L"systemGain", std::to_wstring(s.systemAmplifyPercent));
    AddLine(&text, L"mix", s.mixMode == MixMode::Merge ? L"merge" : L"separate");
    AddLine(&text, L"mixMicrophone", std::to_wstring(s.mergeMicPercent));
    AddLine(&text, L"transcribeMicrophone", OnOff(s.transcribeMicrophone));
    AddLine(&text, L"transcribeSystem", OnOff(s.transcribeSystem));
    AddLine(&text, L"transcribeModel", s.transcribeModel);
    AddLine(&text, L"transcribeLanguage", s.transcribeLanguage == SpeechLanguage::English ? L"en" : L"auto");
    AddLine(&text, L"transcribeGpu", OnOff(s.transcribeGpu));
    AddLine(&text, L"startHotkey", FormatHotkey(saved.hotkeys[HotkeyIndex(HotkeyAction::Start)]));
    AddLine(&text, L"stopHotkey", FormatHotkey(saved.hotkeys[HotkeyIndex(HotkeyAction::Stop)]));
    return text;
}

void ParseSavedSettings(const std::wstring &text, SavedSettings *saved)
{
    for (std::size_t begin = 0; begin < text.size();) {
        std::size_t end = text.find(L'\n', begin);
        if (end == std::wstring::npos)
            end = text.size();
        const std::wstring line = Trimmed(text.substr(begin, end - begin));
        begin = end + 1;

        const std::size_t equals = line.find(L'=');
        if (line.empty() || line[0] == L'#' || equals == std::wstring::npos)
            continue;
        Apply(saved, Trimmed(line.substr(0, equals)), Trimmed(line.substr(equals + 1)));
    }

    /* Edited by hand, the file may switch both streams off, or give both hotkeys the same keys. */
    RecordingSettings &s = saved->recording;
    if (!s.includeMicrophone && !s.includeSystemAudio)
        s.includeMicrophone = true;
    Hotkey *keys = saved->hotkeys;
    if (keys[HotkeyIndex(HotkeyAction::Start)] == keys[HotkeyIndex(HotkeyAction::Stop)]) {
        keys[HotkeyIndex(HotkeyAction::Start)] = DefaultHotkey(HotkeyAction::Start);
        keys[HotkeyIndex(HotkeyAction::Stop)] = DefaultHotkey(HotkeyAction::Stop);
    }
}

}  // namespace audiorgui
