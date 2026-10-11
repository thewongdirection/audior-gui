#include "cli.h"

#include <utility>

namespace audiorgui {

namespace {

constexpr std::size_t kMaxOutputArgChars = 247;         /* audior's limit for --output */
constexpr unsigned    kMinShotSize = 200, kMaxShotSize = 8192;

bool IsSeparator(wchar_t c) { return c == L'\\' || c == L'/'; }

std::wstring Lowered(std::wstring text)
{
    for (wchar_t &c : text)
        c = c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    return text;
}

/* A run of ASCII digits that fits in an int; false for anything else. */
bool ToInt(const std::wstring &text, int *value)
{
    if (text.empty() || text.size() > 9)
        return false;
    int v = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9')
            return false;
        v = v * 10 + (c - L'0');
    }
    *value = v;
    return true;
}

bool IsAbsolute(const std::wstring &path)
{
    return (path.size() >= 2 && path[1] == L':') || (!path.empty() && IsSeparator(path[0]));
}

/* "folder\name" into the settings.  False, with *error, if it cannot be used. */
bool SetOutput(const std::wstring &path, const std::wstring &workingDirectory, RecordingSettings *s,
               std::wstring *error)
{
    if (path.size() > kMaxOutputArgChars) {
        *error = L"output path is too long.";
        return false;
    }
    const std::size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    if (name.empty()) {
        *error = L"--output needs a file name.";
        return false;
    }
    *error = FileNameProblem(name);
    if (!error->empty())
        return false;

    std::wstring dir = slash == std::wstring::npos ? workingDirectory : path.substr(0, slash + 1);
    while (dir.size() > 1 && IsSeparator(dir.back()) && !(dir.size() == 3 && dir[1] == L':'))
        dir.pop_back();                                 /* "C:\rec\" -> "C:\rec", but "C:\" stays */
    s->outputDirectory = IsAbsolute(dir) ? dir : JoinPath(workingDirectory, dir);
    s->fileName = name;
    return true;
}

/* "1120x720" within the allowed range. */
bool ParseSize(const std::wstring &text, unsigned *width, unsigned *height)
{
    const std::size_t x = text.find_first_of(L"xX");
    int w = 0, h = 0;
    if (x == std::wstring::npos || !ToInt(text.substr(0, x), &w) || !ToInt(text.substr(x + 1), &h))
        return false;
    if (w < static_cast<int>(kMinShotSize) || w > static_cast<int>(kMaxShotSize) ||
        h < static_cast<int>(kMinShotSize) || h > static_cast<int>(kMaxShotSize))
        return false;
    *width = static_cast<unsigned>(w);
    *height = static_cast<unsigned>(h);
    return true;
}

/* A percentage for `option`: clamped to the range with a warning, an error if it is not a number. */
bool ParsePercent(const std::wstring &option, const std::wstring &text, int maximum, int *value, CliResult *r)
{
    int v = 0;
    if (!ToInt(text, &v)) {
        r->error = L"invalid " + option + L" '" + text + L"' (use 0-" + std::to_wstring(maximum) + L").";
        return false;
    }
    if (v > maximum) {
        r->warnings.push_back(option + L" " + text + L" is above " + std::to_wstring(maximum) + L"; using " +
                              std::to_wstring(maximum) + L".");
        v = maximum;
    }
    *value = v;
    return true;
}

}  // namespace

CliResult ParseCommandLine(const std::vector<std::wstring> &args, const std::wstring &workingDirectory,
                           RecordingSettings *settings)
{
    CliResult r;
    RecordingSettings &s = *settings;

    for (std::size_t i = 0; i < args.size() && r.error.empty(); ++i) {
        const std::wstring option = Lowered(args[i]);
        const std::wstring shown = args[i];

        /* The value of an option that needs one: the next argument, unless that is another option. */
        std::wstring value;
        auto needValue = [&]() {
            if (i + 1 >= args.size() || (args[i + 1].size() > 1 && args[i + 1][0] == L'-')) {
                r.error = shown + L" needs a value.";
                return false;
            }
            value = args[++i];
            return true;
        };
        auto command = [&](CliResult::Command c) {
            if (r.command == CliResult::Command::Run)
                r.command = c;
        };

        if (option == L"-o" || option == L"--output") {
            if (needValue() && SetOutput(value, workingDirectory, &s, &r.error))
                r.start = true;
        } else if (option == L"-f" || option == L"--format") {
            if (!needValue())
                continue;
            const std::wstring format = Lowered(value);
            if (format == L"mp3" || format == L"wav")
                s.format = format == L"mp3" ? Format::Mp3 : Format::Wav;
            else
                r.error = L"invalid --format '" + value + L"' (use wav or mp3).";
        } else if (option == L"-b" || option == L"--bitrate") {
            if (!needValue())
                continue;
            int kbps = 0;
            if (ToInt(value, &kbps) && IsValidMp3Bitrate(kbps))
                s.mp3BitrateKbps = kbps;
            else
                r.error = L"invalid --bitrate '" + value + L"' (use 96, 112, 128, 160, 192, 224, 256 or 320).";
        } else if (option == L"-i" || option == L"--include-output") {
            s.includeSystemAudio = true;
        } else if (option == L"--merge") {
            s.mixMode = MixMode::Merge;
            int ignored = 0;
            if (i + 1 < args.size() && ToInt(args[i + 1], &ignored)) {      /* the optional percentage */
                ++i;
                int percent = 0;
                if (ParsePercent(shown, args[i], 100, &percent, &r))
                    s.mergeMicPercent = percent;
            }
        } else if (option == L"--separate") {
            s.mixMode = MixMode::Separate;
        } else if (option == L"-m" || option == L"--mic") {
            if (needValue())
                r.microphone = value;
        } else if (option == L"-d" || option == L"--output-device") {
            if (needValue())
                r.outputDevice = value;
        } else if (option == L"-a" || option == L"--amplify") {
            if (needValue())
                ParsePercent(L"--amplify", value, kMaxAmplifyPercent, &s.micAmplifyPercent, &r);
        } else if (option == L"--system-amplify") {
            if (needValue())
                ParsePercent(L"--system-amplify", value, kMaxAmplifyPercent, &s.systemAmplifyPercent, &r);
        } else if (option == L"-l" || option == L"--list-devices") {
            command(CliResult::Command::ListDevices);
        } else if (option == L"-s" || option == L"--stop") {
            command(CliResult::Command::Stop);
        } else if (option == L"-h" || option == L"--help" || option == L"/?") {
            command(CliResult::Command::Help);
        } else if (option == L"--demo") {
            r.demo = true;
        } else if (option == L"--screenshot") {
            if (needValue())
                r.screenshotPath = value;
        } else if (option == L"--scenario") {
            if (needValue())
                r.scenario = value;
        } else if (option == L"--size") {
            if (needValue() && !ParseSize(value, &r.width, &r.height))
                r.error = L"invalid --size '" + value + L"' (use WIDTHxHEIGHT, 200 to 8192).";
        } else {
            r.error = L"unknown option '" + shown + L"'.";
        }
    }
    return r;
}

bool ResolveDevice(const std::vector<DeviceInfo> &devices, const std::wstring &selector, std::wstring *id)
{
    int index = 0;
    const bool numeric = ToInt(selector, &index);
    if (numeric && static_cast<std::size_t>(index) < devices.size()) {
        *id = devices[static_cast<std::size_t>(index)].id;
        return true;
    }
    const std::wstring needle = Lowered(selector);
    if (needle.empty())
        return false;
    if (!numeric) {                                             /* IDs are full of digits: a number never matches one */
        for (const DeviceInfo &d : devices) {                   /* an ID, whole or in part */
            if (Lowered(d.id).find(needle) != std::wstring::npos) {
                *id = d.id;
                return true;
            }
        }
    }
    for (const DeviceInfo &d : devices) {                       /* a name, in part         */
        if (Lowered(d.name).find(needle) != std::wstring::npos) {
            *id = d.id;
            return true;
        }
    }
    return false;
}

namespace {

void AppendDevices(std::wstring *out, const wchar_t *heading, const std::vector<DeviceInfo> &devices)
{
    *out += heading;
    *out += L"\n";
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const DeviceInfo &d = devices[i];
        *out += L"  [" + std::to_wstring(i) + L"] " + d.name + (d.isDefault ? L"  [DEFAULT]" : L"") + L"\n";
        *out += L"      ID: " + d.id + L"\n";
    }
    if (devices.empty())
        *out += L"  (none found)\n";
}

}  // namespace

std::wstring FormatDeviceList(const std::vector<DeviceInfo> &microphones, const std::vector<DeviceInfo> &playback)
{
    std::wstring text = L"=== Audio Devices ===\n\n";
    AppendDevices(&text, L"Microphones / Capture Devices:", microphones);
    text += L"\n";
    AppendDevices(&text, L"Speakers / Playback Devices:", playback);
    return text;
}

std::wstring HelpText()
{
    return L"Audior-GUI - record the microphone and/or the sound your PC plays\n"
           L"\n"
           L"Usage: audior_gui [options]\n"
           L"\n"
           L"With no options the window opens. Options preset the window; giving --output starts\n"
           L"recording straight away. The options are audior's, plus --system-amplify and --demo.\n"
           L"\n"
           L"Recording:\n"
           L"  -o, --output <file>          File to record to (at most 247 characters). The extension\n"
           L"                               follows the format: \"meeting\" becomes meeting.mp3.\n"
           L"  -f, --format <wav|mp3>       Output format (default mp3).\n"
           L"  -b, --bitrate <kbps>         MP3 bitrate: 96, 112, 128, 160, 192, 224, 256 or 320\n"
           L"                               (default 128).\n"
           L"  -m, --mic <id|index|name>    Microphone to record (default: the system default).\n"
           L"  -a, --amplify <0-300>        Boost the microphone: 100 = twice as loud, 300 = four times\n"
           L"                               (default 0).\n"
           L"  -i, --include-output         Also record system audio (what the PC plays).\n"
           L"  -d, --output-device <id|index|name>\n"
           L"                               Playback device to capture (default: the system default).\n"
           L"      --system-amplify <0-300> Boost the system audio the same way (default 0).\n"
           L"      --merge [percent]        Mix both streams into one file. The percentage is the\n"
           L"                               microphone's share of the mix (default 65).\n"
           L"      --separate               Write the streams to <name>_mic and <name>_system files.\n"
           L"\n"
           L"Other:\n"
           L"  -l, --list-devices           List the audio devices and exit.\n"
           L"  -s, --stop                   Stop the recording in progress and exit.\n"
           L"      --demo                   Simulated audio: nothing is recorded or written.\n"
           L"  -h, --help                   Show this help and exit.\n";
}

}  // namespace audiorgui
