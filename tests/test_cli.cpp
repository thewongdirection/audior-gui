#include "../src/core/cli.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

const wchar_t kCwd[] = L"C:\\Work";

struct Parsed {
    CliResult         result;
    RecordingSettings settings;
};

Parsed Parse(std::vector<std::wstring> args)
{
    Parsed p;
    p.settings.outputDirectory = L"C:\\Docs\\audior";                   /* the app's default folder */
    p.result = ParseCommandLine(args, kCwd, &p.settings);
    return p;
}

std::vector<DeviceInfo> Devices()
{
    return {{L"{0.0.1.00000000}.{aaaa1111}", L"Microphone (USB Audio)", false},
            {L"{0.0.1.00000000}.{bbbb2222}", L"Microphone Array (Realtek(R) Audio)", true}};
}

}  // namespace

TEST(Cli_NoArgumentsOpenTheWindowWithTheDefaults)
{
    const Parsed p = Parse({});
    CHECK(p.result.command == CliResult::Command::Run);
    CHECK(p.result.error.empty());
    CHECK(!p.result.start);
    CHECK(!p.result.demo);
    CHECK_EQ(p.settings.outputDirectory, std::wstring(L"C:\\Docs\\audior"));
    CHECK(p.settings.format == Format::Mp3);
}

TEST(Cli_TheCommands)
{
    CHECK(Parse({L"--help"}).result.command == CliResult::Command::Help);
    CHECK(Parse({L"-h"}).result.command == CliResult::Command::Help);
    CHECK(Parse({L"-l"}).result.command == CliResult::Command::ListDevices);
    CHECK(Parse({L"--list-devices"}).result.command == CliResult::Command::ListDevices);
    CHECK(Parse({L"-s"}).result.command == CliResult::Command::Stop);
    CHECK(Parse({L"--stop"}).result.command == CliResult::Command::Stop);
    CHECK(Parse({L"--HELP"}).result.command == CliResult::Command::Help);          /* options ignore case */
}

TEST(Cli_OutputSplitsFolderAndNameAndStartsRecording)
{
    Parsed p = Parse({L"--output", L"D:\\talks\\keynote.mp3"});
    CHECK(p.result.error.empty());
    CHECK(p.result.start);
    CHECK_EQ(p.settings.outputDirectory, std::wstring(L"D:\\talks"));
    CHECK_EQ(p.settings.fileName, std::wstring(L"keynote.mp3"));

    p = Parse({L"-o", L"meeting"});                                 /* no folder: the working directory */
    CHECK_EQ(p.settings.outputDirectory, std::wstring(kCwd));
    CHECK_EQ(p.settings.fileName, std::wstring(L"meeting"));

    p = Parse({L"-o", L"sub\\clip.wav"});                           /* relative to the working directory */
    CHECK_EQ(p.settings.outputDirectory, std::wstring(L"C:\\Work\\sub"));

    p = Parse({L"-o", L"C:\\x.wav"});                               /* a drive root keeps its backslash */
    CHECK_EQ(p.settings.outputDirectory, std::wstring(L"C:\\"));
    CHECK_EQ(p.settings.fileName, std::wstring(L"x.wav"));

    p = Parse({L"-o", L"C:/forward/slashes.mp3"});
    CHECK_EQ(p.settings.outputDirectory, std::wstring(L"C:/forward"));
}

TEST(Cli_OutputIsCheckedLikeAudior)
{
    CHECK(!Parse({L"-o"}).result.error.empty());                    /* needs a value */
    CHECK(!Parse({L"-o", L"-i"}).result.error.empty());             /* ... which is not another option */
    CHECK(!Parse({L"-o", L"C:\\dir\\"}).result.error.empty());      /* no file name */
    CHECK(!Parse({L"-o", L"a?b.mp3"}).result.error.empty());
    CHECK(!Parse({L"-o", L"C:\\nul.mp3"}).result.error.empty());

    const std::wstring longest = L"C:\\" + std::wstring(240, L'd') + L"\\x" + L"yy";        /* 247 characters */
    CHECK_EQ(longest.size(), std::size_t(247));
    CHECK(Parse({L"-o", longest}).result.error.empty());                        /* audior's limit */
    const Parsed p = Parse({L"-o", longest + L"y"});                            /* 248 */
    CHECK(p.result.error.find(L"too long") != std::wstring::npos);
}

TEST(Cli_FormatAndBitrate)
{
    CHECK(Parse({L"-f", L"wav"}).settings.format == Format::Wav);
    CHECK(Parse({L"--format", L"WAV"}).settings.format == Format::Wav);
    CHECK(Parse({L"-f", L"mp3"}).settings.format == Format::Mp3);
    CHECK(!Parse({L"-f", L"flac"}).result.error.empty());

    CHECK_EQ(Parse({L"-b", L"320"}).settings.mp3BitrateKbps, 320);
    CHECK_EQ(Parse({L"--bitrate", L"96"}).settings.mp3BitrateKbps, 96);
    CHECK(!Parse({L"-b", L"100"}).result.error.empty());
    CHECK(!Parse({L"-b", L"abc"}).result.error.empty());
    CHECK(!Parse({L"-b"}).result.error.empty());
    CHECK_EQ(Parse({L"-b", L"100"}).settings.mp3BitrateKbps, 128);  /* a bad value changes nothing */
}

TEST(Cli_AmplifyIsClampedWithAWarning)
{
    Parsed p = Parse({L"-a", L"150"});
    CHECK_EQ(p.settings.micAmplifyPercent, 150);
    CHECK(p.result.warnings.empty());

    p = Parse({L"--amplify", L"500"});
    CHECK(p.result.error.empty());
    CHECK_EQ(p.settings.micAmplifyPercent, 300);
    CHECK_EQ(p.result.warnings.size(), std::size_t(1));

    CHECK(!Parse({L"-a", L"abc"}).result.error.empty());
    CHECK(!Parse({L"-a", L"-i"}).result.error.empty());             /* audior: -a followed by an option */
    CHECK(!Parse({L"-a", L"-5"}).result.error.empty());

    p = Parse({L"--system-amplify", L"200"});
    CHECK_EQ(p.settings.systemAmplifyPercent, 200);
    CHECK_EQ(p.settings.micAmplifyPercent, 0);
}

TEST(Cli_IncludeMergeAndSeparate)
{
    Parsed p = Parse({L"-i"});
    CHECK(p.settings.includeSystemAudio);
    CHECK(p.settings.mixMode == MixMode::Merge);

    p = Parse({L"-i", L"--separate"});
    CHECK(p.settings.mixMode == MixMode::Separate);

    p = Parse({L"-i", L"--merge", L"80"});
    CHECK(p.settings.mixMode == MixMode::Merge);
    CHECK_EQ(p.settings.mergeMicPercent, 80);

    p = Parse({L"--merge", L"-i"});                                 /* the percentage is optional */
    CHECK(p.result.error.empty());
    CHECK_EQ(p.settings.mergeMicPercent, 65);
    CHECK(p.settings.includeSystemAudio);

    p = Parse({L"--merge", L"0"});
    CHECK_EQ(p.settings.mergeMicPercent, 0);
    p = Parse({L"--merge", L"250"});
    CHECK_EQ(p.settings.mergeMicPercent, 100);
    CHECK_EQ(p.result.warnings.size(), std::size_t(1));
}

TEST(Cli_DeviceSelectorsAreKeptForLaterLookup)
{
    const Parsed p = Parse({L"-m", L"Realtek", L"-d", L"1"});
    CHECK_EQ(p.result.microphone, std::wstring(L"Realtek"));
    CHECK_EQ(p.result.outputDevice, std::wstring(L"1"));
    CHECK(p.settings.microphoneId.empty());                         /* not resolved by the parser */
    CHECK(!Parse({L"--mic"}).result.error.empty());
}

TEST(Cli_UnknownOptionsAreRejected)
{
    const Parsed p = Parse({L"--frobnicate"});
    CHECK(p.result.error.find(L"--frobnicate") != std::wstring::npos);
    CHECK(!Parse({L"stray"}).result.error.empty());
}

TEST(Cli_DeveloperOptions)
{
    Parsed p = Parse({L"--demo"});
    CHECK(p.result.demo);

    p = Parse({L"--screenshot", L"out.png", L"--scenario", L"recording", L"--size", L"800x600"});
    CHECK_EQ(p.result.screenshotPath, std::wstring(L"out.png"));
    CHECK_EQ(p.result.scenario, std::wstring(L"recording"));
    CHECK_EQ(p.result.width, 800u);
    CHECK_EQ(p.result.height, 600u);

    CHECK(!Parse({L"--size", L"100x100"}).result.error.empty());
    CHECK(!Parse({L"--size", L"big"}).result.error.empty());
}

TEST(Cli_AFullAudiorCommandLine)
{
    const Parsed p = Parse({L"--output", L"call.wav", L"--format", L"wav", L"--include-output", L"--separate",
                            L"--mic", L"0", L"--amplify", L"50", L"-d", L"Speakers"});
    CHECK(p.result.error.empty());
    CHECK(p.result.start);
    CHECK_EQ(p.settings.fileName, std::wstring(L"call.wav"));
    CHECK(p.settings.format == Format::Wav);
    CHECK(p.settings.includeSystemAudio);
    CHECK(p.settings.mixMode == MixMode::Separate);
    CHECK_EQ(p.settings.micAmplifyPercent, 50);
    CHECK_EQ(p.result.microphone, std::wstring(L"0"));
    CHECK_EQ(p.result.outputDevice, std::wstring(L"Speakers"));
}

TEST(ResolveDevice_ByIndexIdOrName)
{
    const std::vector<DeviceInfo> devices = Devices();
    std::wstring id;
    CHECK(ResolveDevice(devices, L"1", &id));
    CHECK_EQ(id, devices[1].id);
    CHECK(ResolveDevice(devices, L"0", &id));
    CHECK_EQ(id, devices[0].id);

    CHECK(ResolveDevice(devices, L"bbbb2222", &id));                /* part of the ID */
    CHECK_EQ(id, devices[1].id);
    CHECK(ResolveDevice(devices, devices[0].id, &id));              /* the whole ID   */
    CHECK_EQ(id, devices[0].id);

    CHECK(ResolveDevice(devices, L"realtek", &id));                 /* part of the name, any case */
    CHECK_EQ(id, devices[1].id);
    CHECK(ResolveDevice(devices, L"USB", &id));
    CHECK_EQ(id, devices[0].id);
}

TEST(ResolveDevice_FailsForUnknownDevices)
{
    const std::vector<DeviceInfo> devices = Devices();
    std::wstring id = L"unchanged";
    CHECK(!ResolveDevice(devices, L"2", &id));                      /* past the end of the list, though an ID contains a 2 */
    CHECK(!ResolveDevice(devices, L"nonesuch", &id));
    CHECK(!ResolveDevice(devices, L"", &id));
    CHECK(!ResolveDevice({}, L"0", &id));
    CHECK_EQ(id, std::wstring(L"unchanged"));
}

TEST(ResolveDevice_ANumberThatIsNotAnIndexMayStillBeAName)
{
    const std::vector<DeviceInfo> devices = {{L"{id-1}", L"USB 2.0 Mic 7", false}};
    std::wstring id;
    CHECK(ResolveDevice(devices, L"7", &id));                       /* not an index (only one device): a name */
    CHECK_EQ(id, devices[0].id);
}

TEST(FormatDeviceList_MarksDefaultsAndShowsIds)
{
    const std::wstring text = FormatDeviceList(Devices(), {{L"{play}", L"Speakers (Realtek)", true}});
    CHECK(text.find(L"=== Audio Devices ===") != std::wstring::npos);
    CHECK(text.find(L"Microphones / Capture Devices:") != std::wstring::npos);
    CHECK(text.find(L"  [0] Microphone (USB Audio)\n") != std::wstring::npos);
    CHECK(text.find(L"  [1] Microphone Array (Realtek(R) Audio)  [DEFAULT]\n") != std::wstring::npos);
    CHECK(text.find(L"      ID: {0.0.1.00000000}.{bbbb2222}\n") != std::wstring::npos);
    CHECK(text.find(L"Speakers / Playback Devices:") != std::wstring::npos);
    CHECK(text.find(L"  [0] Speakers (Realtek)  [DEFAULT]\n") != std::wstring::npos);
    CHECK(FormatDeviceList({}, {}).find(L"(none found)") != std::wstring::npos);
}

TEST(HelpText_MentionsEveryOption)
{
    const std::wstring help = HelpText();
    CHECK(help.find(L"Audior-GUI") != std::wstring::npos);              /* the program's name */
    const wchar_t *const options[] = {L"--output", L"--format", L"--bitrate", L"--mic", L"--amplify",
                                      L"--include-output", L"--output-device", L"--merge", L"--separate",
                                      L"--list-devices", L"--stop", L"--help", L"--system-amplify", L"--demo",
                                      L"-o,", L"-f,", L"-b,", L"-m,", L"-a,", L"-i,", L"-d,", L"-l,", L"-s,", L"-h,"};
    for (const wchar_t *option : options)
        CHECK(help.find(option) != std::wstring::npos);
}
