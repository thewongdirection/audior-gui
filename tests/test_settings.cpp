#include "../src/core/settings.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

RecordingSettings Valid()
{
    RecordingSettings s;
    s.outputDirectory = L"C:\\Recordings";
    return s;
}

}  // namespace

TEST(Settings_DefaultsMatchAudior)
{
    RecordingSettings s;
    CHECK(s.format == Format::Mp3);
    CHECK_EQ(s.mp3BitrateKbps, 128);
    CHECK_EQ(s.micAmplifyPercent, 0);
    CHECK(s.includeMicrophone);
    CHECK(!s.includeSystemAudio);
    CHECK(s.mixMode == MixMode::Merge);
    CHECK_EQ(s.mergeMicPercent, 65);
    CHECK(s.microphoneId.empty());
    CHECK(s.systemDeviceId.empty());
}

TEST(Settings_Mp3BitrateList)
{
    const int valid[] = {96, 112, 128, 160, 192, 224, 256, 320};
    for (int kbps : valid)
        CHECK(IsValidMp3Bitrate(kbps));
    CHECK_EQ(kMp3BitrateCount, 8);

    const int invalid[] = {-1, 0, 64, 80, 95, 97, 129, 321, 1000};
    for (int kbps : invalid)
        CHECK(!IsValidMp3Bitrate(kbps));
}

TEST(Validate_AcceptsDefaultsWithAFolder)
{
    CHECK(Validate(Valid()).empty());
}

TEST(Validate_RequiresAnOutputFolder)
{
    RecordingSettings s;
    CHECK(!Validate(s).empty());
}

TEST(Validate_RejectsBadBitrateForMp3Only)
{
    RecordingSettings s = Valid();
    s.mp3BitrateKbps = 100;
    CHECK(!Validate(s).empty());
    s.format = Format::Wav;                 /* audior ignores the bitrate for WAV */
    CHECK(Validate(s).empty());
}

TEST(Validate_AmplifyBounds)
{
    RecordingSettings s = Valid();
    s.micAmplifyPercent = 0;
    CHECK(Validate(s).empty());
    s.micAmplifyPercent = 300;
    CHECK(Validate(s).empty());
    s.micAmplifyPercent = 301;
    CHECK(!Validate(s).empty());
    s.micAmplifyPercent = -1;
    CHECK(!Validate(s).empty());
}

TEST(Validate_MergeShareBounds)
{
    RecordingSettings s = Valid();
    s.mergeMicPercent = 0;
    CHECK(Validate(s).empty());
    s.mergeMicPercent = 100;
    CHECK(Validate(s).empty());
    s.mergeMicPercent = 101;
    CHECK(!Validate(s).empty());
    s.mergeMicPercent = -5;
    CHECK(!Validate(s).empty());
}

TEST(DefaultBaseName_IsSortableAndZeroPadded)
{
    LocalTime t;
    t.year = 2026; t.month = 3; t.day = 7; t.hour = 9; t.minute = 5; t.second = 2;
    CHECK_EQ(DefaultBaseName(t), std::wstring(L"audior-20260307-090502"));
    t.month = 12; t.day = 31; t.hour = 23; t.minute = 59; t.second = 59;
    CHECK_EQ(DefaultBaseName(t), std::wstring(L"audior-20261231-235959"));
}

TEST(ExtensionFor_MatchesFormat)
{
    CHECK_EQ(std::wstring(ExtensionFor(Format::Mp3)), std::wstring(L".mp3"));
    CHECK_EQ(std::wstring(ExtensionFor(Format::Wav)), std::wstring(L".wav"));
}

TEST(JoinPath_DoesNotDoubleTheSeparator)
{
    CHECK_EQ(JoinPath(L"C:\\a", L"x.mp3"), std::wstring(L"C:\\a\\x.mp3"));
    CHECK_EQ(JoinPath(L"C:\\a\\", L"x.mp3"), std::wstring(L"C:\\a\\x.mp3"));
    CHECK_EQ(JoinPath(L"C:/a/", L"x.mp3"), std::wstring(L"C:/a/x.mp3"));
    CHECK_EQ(JoinPath(L"", L"x.mp3"), std::wstring(L"x.mp3"));
    CHECK_EQ(JoinPath(L"C:\\", L"x.mp3"), std::wstring(L"C:\\x.mp3"));
}

TEST(SuffixedPath_InsertsBeforeTheExtension)
{
    CHECK_EQ(SuffixedPath(L"C:\\a\\rec.wav", L"_mic"), std::wstring(L"C:\\a\\rec_mic.wav"));
    CHECK_EQ(SuffixedPath(L"rec.tar.mp3", L"_system"), std::wstring(L"rec.tar_system.mp3"));
}

TEST(SuffixedPath_IgnoresDotsInFolderNames)
{
    CHECK_EQ(SuffixedPath(L"C:\\my.folder\\rec", L"_mic"), std::wstring(L"C:\\my.folder\\rec_mic"));
    CHECK_EQ(SuffixedPath(L"C:/v1.2/rec", L"_mic"), std::wstring(L"C:/v1.2/rec_mic"));
}

TEST(PlannedOutputFiles_OneFileUnlessSeparate)
{
    RecordingSettings s = Valid();
    const std::wstring path = L"C:\\r\\a.mp3";

    CHECK_EQ(PlannedOutputFiles(s, path).size(), std::size_t(1));      /* mic only      */

    s.includeSystemAudio = true;
    s.mixMode = MixMode::Merge;
    CHECK_EQ(PlannedOutputFiles(s, path).size(), std::size_t(1));      /* merged        */

    s.mixMode = MixMode::Separate;
    const auto files = PlannedOutputFiles(s, path);                    /* separate      */
    CHECK_EQ(files.size(), std::size_t(2));
    CHECK_EQ(files[0], std::wstring(L"C:\\r\\a_mic.mp3"));
    CHECK_EQ(files[1], std::wstring(L"C:\\r\\a_system.mp3"));

    s.includeSystemAudio = false;           /* "separate" means nothing without system audio */
    CHECK_EQ(PlannedOutputFiles(s, path).size(), std::size_t(1));
}

TEST(Validate_NeedsAtLeastOneStream)
{
    RecordingSettings s = Valid();
    s.includeMicrophone = false;
    CHECK(!Validate(s).empty());                        /* nothing selected */
    s.includeSystemAudio = true;
    CHECK(Validate(s).empty());                         /* system audio alone is fine */
    s.includeSystemAudio = false;
    s.includeMicrophone = true;
    CHECK(Validate(s).empty());                         /* so is the microphone alone */
}

TEST(PlannedOutputFiles_SeparateTracksNeedBothStreams)
{
    RecordingSettings s = Valid();
    s.mixMode = MixMode::Separate;
    const std::wstring path = L"C:\\r\\a.mp3";

    s.includeMicrophone = true;
    s.includeSystemAudio = false;
    CHECK_EQ(PlannedOutputFiles(s, path).size(), std::size_t(1));      /* microphone only */

    s.includeMicrophone = false;
    s.includeSystemAudio = true;
    CHECK_EQ(PlannedOutputFiles(s, path).size(), std::size_t(1));      /* system audio only */
    CHECK_EQ(PlannedOutputFiles(s, path)[0], path);

    s.includeMicrophone = true;
    CHECK_EQ(PlannedOutputFiles(s, path).size(), std::size_t(2));      /* both: two tracks */
    CHECK(RecordsBoth(s));
    s.includeSystemAudio = false;
    CHECK(!RecordsBoth(s));
}

/* ---- system audio gain and the output file name ---- */

TEST(Settings_SystemGainAndFileNameDefaults)
{
    RecordingSettings s;
    CHECK_EQ(s.systemAmplifyPercent, 0);
    CHECK(s.fileName.empty());                          /* empty: a timestamped name */
}

TEST(Validate_SystemAmplifyBounds)
{
    RecordingSettings s = Valid();
    s.systemAmplifyPercent = 300;
    CHECK(Validate(s).empty());
    s.systemAmplifyPercent = 301;
    CHECK(!Validate(s).empty());
    s.systemAmplifyPercent = -1;
    CHECK(!Validate(s).empty());
}

TEST(EnforceExtension_AppendsReplacesAndKeeps)
{
    CHECK_EQ(EnforceExtension(L"meeting", Format::Mp3), std::wstring(L"meeting.mp3"));
    CHECK_EQ(EnforceExtension(L"meeting", Format::Wav), std::wstring(L"meeting.wav"));
    CHECK_EQ(EnforceExtension(L"clip.wav", Format::Mp3), std::wstring(L"clip.mp3"));
    CHECK_EQ(EnforceExtension(L"clip.mp3", Format::Wav), std::wstring(L"clip.wav"));
    CHECK_EQ(EnforceExtension(L"clip.mp3", Format::Mp3), std::wstring(L"clip.mp3"));
    CHECK_EQ(EnforceExtension(L"Clip.MP3", Format::Mp3), std::wstring(L"Clip.MP3"));    /* case is kept */
    CHECK_EQ(EnforceExtension(L"Clip.Wav", Format::Wav), std::wstring(L"Clip.Wav"));
}

TEST(EnforceExtension_OnlyTheLastDotIsAnExtension)
{
    CHECK_EQ(EnforceExtension(L"take.2", Format::Mp3), std::wstring(L"take.mp3"));      /* audior's rule */
    CHECK_EQ(EnforceExtension(L"v1.2.mp3", Format::Mp3), std::wstring(L"v1.2.mp3"));
    CHECK_EQ(EnforceExtension(L"v1.2.wav", Format::Mp3), std::wstring(L"v1.2.mp3"));
    CHECK_EQ(EnforceExtension(L"meeting.", Format::Mp3), std::wstring(L"meeting.mp3"));
    CHECK_EQ(EnforceExtension(L"", Format::Mp3), std::wstring(L".mp3"));
}

TEST(FileNameProblem_AcceptsOrdinaryNames)
{
    CHECK(FileNameProblem(L"").empty());                /* empty means a timestamped name */
    CHECK(FileNameProblem(L"meeting").empty());
    CHECK(FileNameProblem(L"Team call 2026-10-10 (final)").empty());
    CHECK(FileNameProblem(L"clip.wav").empty());
    CHECK(FileNameProblem(L"v1.2.mp3").empty());
    CHECK(FileNameProblem(L"café 日本語").empty());                      /* non-ASCII is fine */
    CHECK(FileNameProblem(L"console").empty());         /* merely starts with a reserved name */
    CHECK(FileNameProblem(L"com10").empty());
    CHECK(FileNameProblem(L"lpt0").empty());
}

TEST(FileNameProblem_RejectsWhatWindowsCannotStore)
{
    const wchar_t *const bad[] = {L"a:b", L"a/b", L"a\\b", L"a*b", L"a?b", L"a\"b", L"a<b", L"a>b", L"a|b",
                                  L"tab\there", L"line\nbreak"};
    for (const wchar_t *name : bad)
        CHECK(!FileNameProblem(name).empty());
}

TEST(FileNameProblem_RejectsReservedDeviceNamesWithAnyExtension)
{
    const wchar_t *const reserved[] = {L"CON", L"con", L"Prn", L"AUX", L"nul", L"COM1", L"com9", L"LPT1", L"lpt9",
                                       L"con.mp3", L"NUL.txt", L"aux.v2.wav", L"con ", L"COM1 .mp3"};
    for (const wchar_t *name : reserved)
        CHECK(!FileNameProblem(name).empty());
}

TEST(FileNameProblem_RejectsNamesWithNothingBeforeTheExtension)
{
    CHECK(!FileNameProblem(L".mp3").empty());
    CHECK(!FileNameProblem(L"...").empty());
    CHECK(!FileNameProblem(L" ").empty());
    CHECK(!FileNameProblem(L". .").empty());
    CHECK(FileNameProblem(L"a.mp3").empty());
}

TEST(IsFileNameChar_MatchesTheRules)
{
    CHECK(IsFileNameChar(L'a'));
    CHECK(IsFileNameChar(L' '));
    CHECK(IsFileNameChar(L'.'));
    CHECK(IsFileNameChar(L'('));
    CHECK(IsFileNameChar(0x00e9));
    for (wchar_t c : std::wstring(L"<>:\"/\\|?*"))
        CHECK(!IsFileNameChar(c));
    CHECK(!IsFileNameChar(L'\0'));
    CHECK(!IsFileNameChar(L'\t'));
    CHECK(!IsFileNameChar(31));
}

TEST(Validate_ChecksTheFileName)
{
    RecordingSettings s = Valid();
    s.fileName = L"meeting";
    CHECK(Validate(s).empty());
    s.fileName = L"a?b";
    CHECK(!Validate(s).empty());
}
