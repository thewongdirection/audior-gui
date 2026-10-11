#include "saved_settings_samples.h"

using namespace audiorgui;

namespace {

SavedSettings Defaults()
{
    SavedSettings s;
    s.recording.outputDirectory = L"C:\\Docs\\Audior-GUI";                     /* what main() puts there */
    return s;
}

/* `text` read over the defaults. */
SavedSettings Read(const std::wstring &text)
{
    SavedSettings s = Defaults();
    ParseSavedSettings(text, &s);
    return s;
}

bool Has(const std::wstring &text, const std::wstring &part) { return text.find(part) != std::wstring::npos; }

}  // namespace

TEST(SavedSettings_EverySettingComesBackAsSaved)
{
    const SavedSettings unusual = UnusualSettings();
    ExpectSame(Read(FormatSavedSettings(unusual)), unusual);
}

TEST(SavedSettings_TheDefaultsComeBackToo)
{
    /* Read over the unusual set, so that a setting the file forgets would show. */
    SavedSettings s = UnusualSettings();
    ParseSavedSettings(FormatSavedSettings(Defaults()), &s);
    ExpectSame(s, Defaults());
}

TEST(SavedSettings_TheFileIsPlainTextOneSettingToALine)
{
    const std::wstring text = FormatSavedSettings(UnusualSettings());
    CHECK(text[0] == L'#');                                                     /* a note on top, then: */
    CHECK(Has(text, L"\r\nfolder=D:\\Aufnahmen\\M\u00FCzik \u9332\u97F3\r\n"));
    CHECK(Has(text, L"\r\nformat=wav\r\n"));
    CHECK(Has(text, L"\r\nmicrophone=off\r\n"));
    CHECK(Has(text, L"\r\nstartHotkey=Alt+Shift+M\r\n"));
    CHECK(Has(text, L"\r\nstopHotkey=F11\r\n"));
    for (std::size_t i = 0; i < text.size(); ++i)
        CHECK(text[i] != L'\n' || (i > 0 && text[i - 1] == L'\r'));            /* CR LF throughout */
}

TEST(SavedSettings_TheDefaultHotkeysAreWritten)
{
    const std::wstring text = FormatSavedSettings(Defaults());
    CHECK(Has(text, L"startHotkey=Ctrl+Alt+R\r\n"));
    CHECK(Has(text, L"stopHotkey=Ctrl+Alt+S\r\n"));
}

TEST(SavedSettings_NothingToReadChangesNothing)
{
    const SavedSettings unusual = UnusualSettings();
    for (const wchar_t *text : {L"", L"\r\n\r\n", L"# only a note\r\n", L"no equals sign", L"=", L"= value"}) {
        SavedSettings s = unusual;
        ParseSavedSettings(text, &s);
        ExpectSame(s, unusual);
    }
}

TEST(SavedSettings_LineEndingsSpacesAndNotesDoNotMatter)
{
    const SavedSettings s = Read(L"# a note\n"
                                 L"\n"
                                 L"  format = wav  \n"
                                 L"\tbitrate=192\t\r\n"
                                 L"   # an indented note\n"
                                 L"systemAudio=on");                          /* no line end at the end */
    CHECK(s.recording.format == Format::Wav);
    CHECK_EQ(s.recording.mp3BitrateKbps, 192);
    CHECK(s.recording.includeSystemAudio);
}

TEST(SavedSettings_UnknownKeysAreSkippedAndKeysAreCaseSensitive)
{
    const SavedSettings s = Read(L"colour=blue\r\nFormat=wav\r\nformat=wav\r\n");
    CHECK(s.recording.format == Format::Wav);                                  /* the third line */
    CHECK(Read(L"Format=wav\r\n").recording.format == Format::Mp3);
}

TEST(SavedSettings_ALaterLineReplacesAnEarlierOne)
{
    CHECK(Read(L"format=wav\r\nformat=mp3\r\n").recording.format == Format::Mp3);
    CHECK(Read(L"format=wav\r\nformat=ogg\r\n").recording.format == Format::Wav);       /* a bad one does not */
}

TEST(SavedSettings_ABadValueKeepsTheDefaultAndSpoilsNothingElse)
{
    const wchar_t *const bad[] = {
        L"format=ogg",          L"format=",             L"bitrate=100",         L"bitrate=-1",
        L"bitrate=",            L"bitrate=128kbps",     L"microphone=maybe",    L"microphone=true",
        L"microphone=",         L"microphoneGain=301",  L"microphoneGain=-5",   L"microphoneGain=1x",
        L"systemGain=9999999999", L"systemGain=",       L"mixMicrophone=101",   L"mixMicrophone=one",
        L"mix=blend",           L"systemAudio=1",       L"transcribeSystem=",   L"transcribeGpu=gpu",
        L"transcribeMicrophone=yes", L"transcribeLanguage=fr", L"transcribeModel=unknown.bin",
        L"transcribeModel=..\\ggml-small.en-q5_1.bin",
        L"fileName=a:b",        L"fileName=CON.mp3",    L"fileName=.mp3",       L"folder=",
        L"startHotkey=Ctrl+Banana", L"startHotkey=",    L"stopHotkey=S",        L"stopHotkey=Ctrl",
    };
    for (const wchar_t *line : bad) {
        const SavedSettings s = Read(std::wstring(line) + L"\r\nbitrate=224\r\n");
        SavedSettings expected = Defaults();
        expected.recording.mp3BitrateKbps = 224;                               /* the good line after it */
        ExpectSame(s, expected);
    }
}

TEST(SavedSettings_TheEdgesOfTheRangesAreAccepted)
{
    SavedSettings s = Read(L"bitrate=96\r\nmicrophoneGain=300\r\nsystemGain=0\r\nmixMicrophone=100\r\n");
    CHECK_EQ(s.recording.mp3BitrateKbps, 96);
    CHECK_EQ(s.recording.micAmplifyPercent, 300);
    CHECK_EQ(s.recording.systemAmplifyPercent, 0);
    CHECK_EQ(s.recording.mergeMicPercent, 100);
    s = Read(L"bitrate=320\r\nmixMicrophone=0\r\n");
    CHECK_EQ(s.recording.mp3BitrateKbps, 320);
    CHECK_EQ(s.recording.mergeMicPercent, 0);
}

TEST(SavedSettings_AFolderMayBeAsLongAsWindowsAllows)
{
    const std::wstring longest = L"D:\\" + std::wstring(kMaxOutputPathChars - 3, L'a');
    CHECK_EQ(Read(L"folder=" + longest + L"\r\n").recording.outputDirectory, longest);
    CHECK_EQ(Read(L"folder=" + longest + L"a\r\n").recording.outputDirectory, std::wstring(L"C:\\Docs\\Audior-GUI"));
}

TEST(SavedSettings_ATranscriptionModelIsKeptUnderItsNameInTheTable)
{
    CHECK_EQ(Read(L"transcribeModel=GGML-SMALL.EN-Q5_1.BIN\r\n").recording.transcribeModel,
             std::wstring(kAsrModels[0].file));
    SavedSettings s = UnusualSettings();
    ParseSavedSettings(L"transcribeModel=\r\n", &s);                            /* "the first one installed" */
    CHECK(s.recording.transcribeModel.empty());
}

TEST(SavedSettings_BothStreamsOffIsRepaired)
{
    SavedSettings s = Read(L"microphone=off\r\nsystemAudio=off\r\n");
    CHECK(s.recording.includeMicrophone);
    CHECK(!s.recording.includeSystemAudio);
    s = Read(L"microphone=off\r\n");                                           /* the system audio is off by default */
    CHECK(s.recording.includeMicrophone);
    s = Read(L"microphone=off\r\nsystemAudio=on\r\n");                          /* a valid choice stays */
    CHECK(!s.recording.includeMicrophone);
    CHECK(s.recording.includeSystemAudio);
}

TEST(SavedSettings_AValueCannotSmuggleInAnotherLine)
{
    SavedSettings s = Defaults();
    s.recording.microphoneId = L"x\r\nformat=wav";
    s.recording.fileName = L"a\nb";
    s.recording.systemDeviceId = L"{0.0.0.00000000}.{cccc3333}";
    const std::wstring text = FormatSavedSettings(s);
    CHECK(!Has(text, L"microphoneDevice"));                                    /* left out, not escaped */
    CHECK(!Has(text, L"fileName"));
    CHECK(Has(text, L"systemDevice={0.0.0.00000000}.{cccc3333}\r\n"));          /* the rest is still there */
    const SavedSettings back = Read(text);
    CHECK(back.recording.format == Format::Mp3);
    CHECK(back.recording.microphoneId.empty());
    CHECK_EQ(back.recording.systemDeviceId, s.recording.systemDeviceId);
}

TEST(SavedSettings_AFolderMustBeAnAbsolutePath)
{
    const std::wstring defaultFolder = Defaults().recording.outputDirectory;
    CHECK_EQ(Read(L"folder=Recordings\r\n").recording.outputDirectory, defaultFolder);   /* meant from where? */
    CHECK_EQ(Read(L"folder=..\\Rec\r\n").recording.outputDirectory, defaultFolder);
    CHECK_EQ(Read(L"folder=D:\\Rec\r\n").recording.outputDirectory, std::wstring(L"D:\\Rec"));
    CHECK_EQ(Read(L"folder=\\\\server\\share\\Rec\r\n").recording.outputDirectory,
             std::wstring(L"\\\\server\\share\\Rec"));
}

TEST(SavedSettings_NamesAndIdsAreHeldToTheirLimits)
{
    const std::wstring longest(kMaxOutputPathChars, L'a');
    CHECK_EQ(Read(L"fileName=" + longest + L"\r\n").recording.fileName, longest);
    CHECK(Read(L"fileName=" + longest + L"a\r\n").recording.fileName.empty());
    const std::wstring id(512, L'x');
    CHECK_EQ(Read(L"microphoneDevice=" + id + L"\r\n").recording.microphoneId, id);
    CHECK(Read(L"microphoneDevice=" + id + L"x\r\n").recording.microphoneId.empty());
    CHECK(Read(L"systemDevice=" + id + L"x\r\n").recording.systemDeviceId.empty());
}

TEST(SavedSettings_AFileWeWriteAlwaysFitsWhatWeAreWillingToRead)
{
    /* The longest everything may be; at three bytes a UTF-16 unit is the most UTF-8 takes. */
    SavedSettings s = UnusualSettings();
    s.recording.outputDirectory = L"D:\\" + std::wstring(kMaxOutputPathChars - 3, L'\u9332');
    s.recording.fileName = std::wstring(kMaxOutputPathChars, L'\u9332');
    s.recording.microphoneId = s.recording.systemDeviceId = std::wstring(512, L'\u9332');
    const std::wstring text = FormatSavedSettings(s);
    CHECK(text.size() * 3 <= kMaxSavedSettingsBytes);
    ExpectSame(Read(text), s);                                                  /* and none of it was dropped */
}

TEST(SavedSettings_HalfASurrogatePairIsLeftOutNotTheWholeFile)
{
    SavedSettings s = Defaults();
    s.recording.microphoneId = L"mic\xD83D";                                    /* a high half alone */
    s.recording.systemDeviceId = L"\xDE00 speakers";                            /* a low half alone */
    s.recording.fileName = L"clip \xD83D\xDE00";                               /* a whole pair: an emoji */
    s.recording.includeSystemAudio = true;
    const std::wstring text = FormatSavedSettings(s);
    CHECK(!Has(text, L"microphoneDevice"));
    CHECK(!Has(text, L"systemDevice"));
    const SavedSettings back = Read(text);
    CHECK_EQ(back.recording.fileName, s.recording.fileName);
    CHECK(back.recording.includeSystemAudio);
    CHECK(Read(L"microphoneDevice=mic\xD83D\r\n").recording.microphoneId.empty());          /* nor read */
}

TEST(SavedSettings_TheSameKeysForBothHotkeysAreGivenUp)
{
    SavedSettings s = Read(L"startHotkey=Ctrl+Alt+M\r\nstopHotkey=Ctrl+Alt+M\r\nformat=wav\r\n");
    CHECK(s.hotkeys[HotkeyIndex(HotkeyAction::Start)] == DefaultHotkey(HotkeyAction::Start));
    CHECK(s.hotkeys[HotkeyIndex(HotkeyAction::Stop)] == DefaultHotkey(HotkeyAction::Stop));
    CHECK(s.recording.format == Format::Wav);                                   /* the rest is unaffected */

    s = Read(L"startHotkey=Ctrl+Alt+S\r\n");                                   /* the default Stop, but then */
    CHECK(s.hotkeys[HotkeyIndex(HotkeyAction::Start)] == DefaultHotkey(HotkeyAction::Start));
    s = Read(L"startHotkey=Ctrl+Alt+M\r\nstopHotkey=Ctrl+Alt+N\r\n");        /* two different ones stay */
    CHECK(s.hotkeys[HotkeyIndex(HotkeyAction::Start)] == (Hotkey{kModCtrl | kModAlt, 'M'}));
    CHECK(s.hotkeys[HotkeyIndex(HotkeyAction::Stop)] == (Hotkey{kModCtrl | kModAlt, 'N'}));
}

