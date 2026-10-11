#include <cwchar>
#include <fstream>
#include <string>

#include "../src/engine/transcript_file.h"
#include "temp_dir.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

std::string Read(const std::wstring &path)
{
    const std::vector<std::uint8_t> bytes = ReadFileBytes(path);
    return std::string(bytes.begin(), bytes.end());
}

void Write(const std::wstring &path, const std::string &bytes)
{
    std::ofstream out(path, std::ios::binary);
    out << bytes;
}

std::wstring FromUtf8(const std::string &bytes)
{
    std::wstring out(bytes.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()),
                                      &out[0], static_cast<int>(out.size()));
    out.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
    return out;
}

/* An event as the helper sends it: a draft while the utterance is spoken, a final when it settles. */
TranscriptEvent Said(std::uint32_t utterance, double start, const wchar_t *text, bool isFinal = true)
{
    TranscriptEvent e;
    e.utterance = utterance;
    e.isFinal = isFinal;
    e.startSeconds = start;
    e.SetText(text, std::wcslen(text));
    return e;
}

const std::string kBom = "\xEF\xBB\xBF";

}  // namespace

TEST(TranscriptFile_SavesEachPhraseAsALineWithItsTime)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"rec - system audio.txt");
    TranscriptFile file(path);
    file.Add(Said(0, 1.2, L"This is a test."));
    file.Add(Said(1, 66.0, L"The weather today is cold."));
    file.Add(Said(2, 3725.0, L"An hour in."));
    file.Close();

    CHECK(file.Written());
    CHECK_EQ(file.Error(), DWORD(0));
    CHECK_EQ(file.Path(), path);
    /* one byte-order mark at the start, then a line for each, with the clock the panes show */
    CHECK_EQ(Read(path), kBom + "[0:01] This is a test.\r\n[1:06] The weather today is cold.\r\n[1:02:05] An hour in.\r\n");
}

TEST(TranscriptFile_AnyLanguageComesBackAsItWasSpoken)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"t.txt");
    const std::wstring text = L"Привет, мир! 你好，世界。 Καλημέρα café \U0001F600 العربية";    /* includes a surrogate pair */
    TranscriptFile file(path);
    file.Add(Said(0, 5.0, text.c_str()));
    file.Close();

    const std::string bytes = Read(path);
    CHECK_EQ(bytes.compare(0, 3, kBom), 0);
    CHECK_EQ(FromUtf8(bytes.substr(3)), L"[0:05] " + text + L"\r\n");     /* valid UTF-8, nothing lost */
}

TEST(TranscriptFile_NothingToSayLeavesNoFile)
{
    TempDir dir;
    {
        TranscriptFile silent(dir.Path(L"never.txt"));          /* a stream that is never spoken on */
        silent.Close();
        CHECK(!silent.Written());
    }
    {
        TranscriptFile blank(dir.Path(L"blank.txt"));           /* phrases taken back, and nothing but white space */
        blank.Add(Said(0, 1.0, L""));
        blank.Add(Said(1, 2.0, L"   \t "));
        blank.Close();
        CHECK(!blank.Written());
        CHECK_EQ(blank.Error(), DWORD(0));
    }
    CHECK(dir.Names().empty());
}

TEST(TranscriptFile_OnePhraseIsOneLine)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"t.txt");
    TranscriptFile file(path);
    file.Add(Said(0, 0.0, L"first\r\nsecond\tthird"));
    file.Close();
    CHECK_EQ(Read(path), kBom + "[0:00] first  second third\r\n");
}

TEST(TranscriptFile_AFinalReplacesTheDraftsOfItsUtterance)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"t.txt");
    TranscriptFile file(path);
    file.Add(Said(0, 1.0, L"hel", false));                      /* drafts are not lines... */
    file.Add(Said(0, 1.0, L"hello wor", false));
    CHECK(!file.Written());
    file.Add(Said(0, 1.0, L"Hello world.", true));              /* ...the final is, once */
    file.Close();
    CHECK_EQ(Read(path), kBom + "[0:01] Hello world.\r\n");
}

TEST(TranscriptFile_TheWordsBeingSpokenAtTheEndAreKept)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"t.txt");
    TranscriptFile file(path);
    file.Add(Said(0, 1.0, L"Settled.", true));
    file.Add(Said(1, 3.0, L"and the last words, still being spo", false));    /* Stop came, or the helper died, before it settled */
    file.Close();
    CHECK_EQ(Read(path), kBom + "[0:01] Settled.\r\n[0:03] and the last words, still being spo\r\n");
}

TEST(TranscriptFile_AnUtteranceThatNeverSettledIsWrittenAsItStoodWhenTheNextOneBegins)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"t.txt");
    TranscriptFile file(path);
    file.Add(Said(0, 1.0, L"first words", false));              /* the helper was changed to a smaller model: no final comes */
    file.Add(Said(1, 5.0, L"second", false));
    file.Add(Said(1, 5.0, L"Second phrase.", true));
    file.Close();
    CHECK_EQ(Read(path), kBom + "[0:01] first words\r\n[0:05] Second phrase.\r\n");

    TempDir other;                                              /* a final that does come late is not written twice */
    TranscriptFile late(other.Path(L"t.txt"));
    late.Add(Said(0, 1.0, L"first words", false));
    late.Add(Said(1, 5.0, L"second", false));
    late.Add(Said(0, 1.0, L"First words.", true));
    late.Close();
    CHECK_EQ(Read(other.Path(L"t.txt")), kBom + "[0:01] first words\r\n[0:05] second\r\n");
}

TEST(TranscriptFile_APhraseTakenBackIsNotKept)
{
    TempDir dir;
    TranscriptFile file(dir.Path(L"t.txt"));
    file.Add(Said(0, 1.0, L"uh", false));                       /* it turned out to be nothing: an empty final */
    file.Add(Said(0, 1.0, L"", true));
    file.Close();
    CHECK(!file.Written());
    CHECK(dir.Names().empty());
}

TEST(TranscriptFile_AnEventThatComesAfterTheEndMakesNoSecondFile)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"t.txt");
    TranscriptFile file(path);
    file.Add(Said(0, 1.0, L"before"));
    file.Close();
    file.Add(Said(1, 2.0, L"too late"));                        /* e.g. the helper's last words, after Stop gave up waiting */

    CHECK_EQ(Read(path), kBom + "[0:01] before\r\n");
    CHECK_EQ(dir.Names().size(), std::size_t(1));               /* and no "t (2).txt" */

    TranscriptFile never(dir.Path(L"never.txt"));
    never.Close();
    never.Add(Said(0, 1.0, L"too late for a file that was never begun"));
    CHECK(!never.Written());
    CHECK_EQ(dir.Names().size(), std::size_t(1));
}

TEST(TranscriptFile_NeverTakesTheNameOfAFileThatExists)
{
    TempDir dir;
    Write(dir.Path(L"rec - microphone.txt"), "an earlier transcript");
    Write(dir.Path(L"rec - microphone (2).txt"), "another one");

    TranscriptFile file(dir.Path(L"rec - microphone.txt"));
    file.Add(Said(0, 1.0, L"new words"));
    file.Close();

    CHECK_EQ(Read(dir.Path(L"rec - microphone.txt")), std::string("an earlier transcript"));
    CHECK_EQ(Read(dir.Path(L"rec - microphone (2).txt")), std::string("another one"));
    CHECK_EQ(file.Path(), dir.Path(L"rec - microphone (3).txt"));
    CHECK_EQ(Read(file.Path()), kBom + "[0:01] new words\r\n");
}

TEST(TranscriptFile_GivesUpWhenEveryNameIsTaken)
{
    TempDir dir;
    Write(dir.Path(L"t.txt"), "1");
    for (int n = 2; n <= 99; ++n)
        Write(dir.Path(L"t (" + std::to_wstring(n) + L").txt"), "taken");

    TranscriptFile file(dir.Path(L"t.txt"));
    file.Add(Said(0, 1.0, L"nowhere to go"));
    file.Add(Said(1, 2.0, L"and not tried again"));
    file.Close();

    CHECK(!file.Written());
    CHECK(file.Error() != 0);
    CHECK_EQ(dir.Names().size(), std::size_t(99));              /* nothing overwritten, nothing added */
}

TEST(TranscriptFile_MakesItsFolderIfItIsNotThereYet)
{
    TempDir dir;
    const std::wstring path = dir.Path(L"Recordings") + L"\\rec - system audio.txt";
    TranscriptFile file(path);
    file.Add(Said(0, 1.0, L"hello"));
    file.Close();
    CHECK(file.Written());
    CHECK_EQ(Read(path), kBom + "[0:01] hello\r\n");
}

TEST(TranscriptFile_AFileThatCannotBeMadeIsReportedAndNeverEndsAnything)
{
    TempDir dir;
    Write(dir.Path(L"blocker"), "a file where a folder would have to be");
    TranscriptFile file(dir.Path(L"blocker") + L"\\rec - microphone.txt");

    file.Add(Said(0, 1.0, L"lost"));                            /* no folder can be made there */
    file.Add(Said(1, 2.0, L"also lost"));                       /* and it is not tried again, or a crash */
    file.Close();

    CHECK(!file.Written());
    CHECK(file.Error() != 0);
    CHECK_EQ(dir.Names().size(), std::size_t(1));               /* only the blocker */
}
