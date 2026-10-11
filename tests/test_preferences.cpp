#include <windows.h>

#include "../src/preferences.h"
#include "saved_settings_samples.h"
#include "temp_dir.h"

using namespace audiorgui;

namespace {

const wchar_t kFile[] = L"Audior-GUI.ini";

SavedSettings Defaults()
{
    SavedSettings s;
    s.recording.outputDirectory = L"C:\\Docs\\Audior-GUI";
    return s;
}

/* Every setting away from its default.  The folder is one that can be made (it is under the scratch folder, with
 * characters outside ASCII), as a folder that cannot is not remembered. */
SavedSettings Unusual(const TempDir &dir)
{
    SavedSettings s = UnusualSettings();
    s.recording.outputDirectory = dir.Path(L"M\u00FCzik \u9332\u97F3");
    return s;
}

/* What the file holds, read over the defaults. */
SavedSettings Loaded(const Preferences &prefs)
{
    SavedSettings s = Defaults();
    prefs.Load(&s);
    return s;
}

}  // namespace

TEST(Preferences_NoFileMeansTheDefaults)
{
    TempDir dir;
    const Preferences prefs(dir.Path(kFile));
    ExpectSame(Loaded(prefs), Defaults());
}

TEST(Preferences_SettingsComeBackAsSaved)
{
    TempDir dir;
    const SavedSettings unusual = Unusual(dir);            /* folder and name in other scripts: the file is UTF-8 */
    CHECK(Preferences(dir.Path(kFile)).Save(unusual));
    ExpectSame(Loaded(Preferences(dir.Path(kFile))), unusual);      /* a new run */
}

TEST(Preferences_TheFileIsUtf8AndLeavesNothingBeside)
{
    TempDir dir;
    CHECK(Preferences(dir.Path(kFile)).Save(Unusual(dir)));
    CHECK_EQ(dir.Names().size(), std::size_t(1));                   /* no temporary file left behind */

    const std::vector<std::uint8_t> bytes = ReadFileBytes(dir.Path(kFile));
    const std::string text(bytes.begin(), bytes.end());
    CHECK(text.find("\\M\xC3\xBCzik \xE9\x8C\xB2\xE9\x9F\xB3\r\n") != std::string::npos);
    CHECK(text.compare(0, 3, "\xEF\xBB\xBF") != 0);                 /* no byte order mark */
}

TEST(Preferences_ASecondSaveReplacesTheFirst)
{
    TempDir dir;
    const Preferences prefs(dir.Path(kFile));
    CHECK(prefs.Save(Unusual(dir)));
    CHECK(prefs.Save(Defaults()));
    ExpectSame(Loaded(prefs), Defaults());
    CHECK_EQ(dir.Names().size(), std::size_t(1));
}

TEST(Preferences_AFileEditedInNotepadIsRead)
{
    TempDir dir;
    CHECK(WriteFileBytes(dir.Path(kFile), "\xEF\xBB\xBF# edited\r\nformat = wav\r\nbitrate=192\n"));
    const SavedSettings s = Loaded(Preferences(dir.Path(kFile)));
    CHECK(s.recording.format == Format::Wav);                       /* the byte order mark is skipped */
    CHECK_EQ(s.recording.mp3BitrateKbps, 192);
}

TEST(Preferences_AnythingUnreadableMeansTheDefaults)
{
    TempDir dir;
    const Preferences prefs(dir.Path(kFile));

    CHECK(WriteFileBytes(dir.Path(kFile), ""));                     /* empty */
    ExpectSame(Loaded(prefs), Defaults());

    CHECK(WriteFileBytes(dir.Path(kFile), "\xC3\x28 format=wav\n"));         /* not UTF-8, nonsense in any code page */
    ExpectSame(Loaded(prefs), Defaults());

    CHECK(WriteFileBytes(dir.Path(kFile), std::string("f\0o\0r\0m\0a\0t\0=\0w\0a\0v\0", 20)));  /* UTF-16, no mark */
    ExpectSame(Loaded(prefs), Defaults());

    CHECK(WriteFileBytes(dir.Path(kFile), "\xFF\xFE"));                /* a UTF-16 mark and nothing else */
    ExpectSame(Loaded(prefs), Defaults());

    /* A real file with a line that is wrong in the middle: the others still count. */
    CHECK(WriteFileBytes(dir.Path(kFile), "format=wav\nbitrate=banana\nsystemAudio=on\n"));
    const SavedSettings s = Loaded(prefs);
    CHECK(s.recording.format == Format::Wav);
    CHECK_EQ(s.recording.mp3BitrateKbps, kDefaultMp3Bitrate);
    CHECK(s.recording.includeSystemAudio);
}

TEST(Preferences_AFileSavedAsUnicodeOrAnsiIsReadToo)
{
    TempDir dir;
    const Preferences prefs(dir.Path(kFile));

    /* UTF-16 with its byte order mark: what PowerShell 5's `>` and Notepad's "Unicode" write. */
    const std::wstring text = L"# edited\r\nformat=wav\r\nbitrate=192\r\n";
    std::string utf16 = "\xFF\xFE";
    utf16.append(reinterpret_cast<const char *>(text.data()), text.size() * sizeof(wchar_t));
    CHECK(WriteFileBytes(dir.Path(kFile), utf16));
    SavedSettings s = Loaded(prefs);
    CHECK(s.recording.format == Format::Wav);
    CHECK_EQ(s.recording.mp3BitrateKbps, 192);

    /* One byte that is not UTF-8 (an "e" with an acute accent, saved as ANSI) does not cost the other lines. */
    CHECK(WriteFileBytes(dir.Path(kFile), "format=wav\r\nfileName=caf\xE9\r\nbitrate=192\r\n"));
    s = Loaded(prefs);
    CHECK(s.recording.format == Format::Wav);
    CHECK_EQ(s.recording.mp3BitrateKbps, 192);
}

TEST(Preferences_AFileAnEditorHasOpenCanBeRead)
{
    TempDir dir;
    CHECK(WriteFileBytes(dir.Path(kFile), "format=wav\r\n"));
    const HANDLE editor = CreateFileW(dir.Path(kFile).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(editor != INVALID_HANDLE_VALUE);
    CHECK(Loaded(Preferences(dir.Path(kFile))).recording.format == Format::Wav);
    CloseHandle(editor);
}

TEST(Preferences_AFolderThatCannotBeMadeIsNotRemembered)
{
    TempDir dir;
    const Preferences prefs(dir.Path(kFile));
    SavedSettings s = Defaults();
    const std::wstring builtIn = s.recording.outputDirectory;

    s.recording.outputDirectory = dir.Dir();                        /* there */
    CHECK(prefs.Save(s));
    CHECK_EQ(Loaded(prefs).recording.outputDirectory, dir.Dir());

    s.recording.outputDirectory = dir.Path(L"later");               /* not yet, but the recording makes it */
    CHECK(prefs.Save(s));
    CHECK_EQ(Loaded(prefs).recording.outputDirectory, dir.Path(L"later"));

    s.recording.outputDirectory = dir.Path(L"gone\\deeper");         /* nor the folder above it: another PC's? */
    CHECK(prefs.Save(s));
    s = Loaded(prefs);
    CHECK_EQ(s.recording.outputDirectory, builtIn);
}

TEST(Preferences_AFileThatCannotBeReplacedStaysAsItWas)
{
    TempDir dir;
    const Preferences prefs(dir.Path(kFile));
    CHECK(prefs.Save(Unusual(dir)));

    SetFileAttributesW(dir.Path(kFile).c_str(), FILE_ATTRIBUTE_READONLY);
    const bool saved = prefs.Save(Defaults());
    SetFileAttributesW(dir.Path(kFile).c_str(), FILE_ATTRIBUTE_NORMAL);      /* so that the folder can be cleared */
    CHECK(!saved);
    CHECK_EQ(dir.Names().size(), std::size_t(1));                   /* the temporary file was removed again */
    ExpectSame(Loaded(prefs), Unusual(dir));
}

TEST(Preferences_AFileTooBigToBeOursIsIgnored)
{
    TempDir dir;
    const Preferences prefs(dir.Path(kFile));
    const std::string line = "format=wav\n";
    std::string text = line + std::string(kMaxSavedSettingsBytes, '#');
    CHECK(WriteFileBytes(dir.Path(kFile), text));
    CHECK(Loaded(prefs).recording.format == Format::Mp3);

    text.resize(kMaxSavedSettingsBytes);                            /* the biggest there can be */
    CHECK(WriteFileBytes(dir.Path(kFile), text));
    CHECK(Loaded(prefs).recording.format == Format::Wav);
}

TEST(Preferences_AFolderThatCannotBeWrittenToJustForgets)
{
    TempDir dir;
    const Preferences prefs(dir.Path(L"missing\\" + std::wstring(kFile)));      /* its folder does not exist */
    CHECK(!prefs.Save(Unusual(dir)));
    ExpectSame(Loaded(prefs), Defaults());
    CHECK(dir.Names().empty());

    CHECK(!Preferences(std::wstring()).Save(Unusual(dir)));         /* no path at all */
    ExpectSame(Loaded(Preferences(std::wstring())), Defaults());
}

TEST(Preferences_AFailedSaveLeavesTheOldFileAlone)
{
    TempDir dir;
    const Preferences prefs(dir.Path(kFile));
    CHECK(prefs.Save(Unusual(dir)));

    /* Someone has the temporary name open, so the new file cannot be made. */
    const HANDLE blocker = CreateFileW(dir.Path(std::wstring(kFile) + L".tmp").c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(blocker != INVALID_HANDLE_VALUE);
    CHECK(!prefs.Save(Defaults()));
    CloseHandle(blocker);
    ExpectSame(Loaded(prefs), Unusual(dir));
}

TEST(Preferences_TheDefaultFileIsBesideTheProgram)
{
    const std::wstring path = Preferences::DefaultPath();
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring directory(exe, n);
    directory.resize(directory.find_last_of(L'\\'));
    CHECK_EQ(path, directory + L"\\Audior-GUI.ini");
}
