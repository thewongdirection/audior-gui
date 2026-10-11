/*
 * Tests of the real download program (audior_fetch.exe).
 *
 * build.bat builds it next to run_tests.exe; without it a test is skipped and says
 * so.  They check what the program refuses before it touches the network, which is
 * most of what keeps it harmless, so they need no connection (and a download of
 * a real model is a check by hand: docs/FIRST-TEST.md).
 */
#include <string>
#include <vector>

#include "../src/core/fetch_protocol.h"
#include "../src/engine/asr_helper.h"
#include "../src/engine/process_transport.h"
#include "temp_dir.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

std::wstring FetcherPath() { return ProgramDirectory() + L"\\" + kFetcherFile; }

bool FetcherExists() { return GetFileAttributesW(FetcherPath().c_str()) != INVALID_FILE_ATTRIBUTES; }

/* Runs the program with `arguments` and returns what it said, line by line. */
std::vector<fetch::Line> Run(const std::wstring &arguments)
{
    std::vector<fetch::Line> lines;
    std::wstring problem;
    std::unique_ptr<ProcessTransport> program = ProcessTransport::Launch(FetcherPath(), arguments, ProcessTransport::Limits(), &problem);
    CHECK(program != nullptr);
    if (!program)
        return lines;
    fetch::LineReader reader;
    char bytes[512];
    for (std::size_t n; (n = program->Read(bytes, sizeof(bytes))) > 0;)
        reader.Append(bytes, n, [&](const fetch::Line &line) { lines.push_back(line); });
    return lines;
}

const std::wstring kModel = L"https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin";

/* The program is given `arguments`; it must answer with one error naming `reason`, and write nothing. */
void Refuses(const TempDir &dir, const std::wstring &arguments, const wchar_t *reason)
{
    const std::vector<fetch::Line> lines = Run(arguments);
    CHECK_EQ(lines.size(), std::size_t(1));
    if (lines.size() == 1) {
        CHECK(lines[0].kind == fetch::Line::Kind::Error);
        CHECK(lines[0].text.find(reason) != std::wstring::npos);
    }
    CHECK(dir.Names().empty());
}

}  // namespace

TEST(RealFetcher_OnlyHttpsAddressesOnHuggingFaceAreAccepted)
{
    if (!FetcherExists())
        SKIP("audior_fetch.exe is not next to run_tests.exe (build.bat)");
    TempDir dir;
    const std::wstring file = L"\"" + dir.Path(L"a.part") + L"\" 100";
    const wchar_t *reason = L"only https addresses on huggingface.co";
    Refuses(dir, L"\"http://huggingface.co/x\" " + file, reason);                   /* not encrypted */
    Refuses(dir, L"\"ftp://huggingface.co/x\" " + file, reason);
    Refuses(dir, L"\"https://example.com/x\" " + file, reason);                     /* another host */
    Refuses(dir, L"\"https://huggingface.co.example.com/x\" " + file, reason);      /* one that only begins like it */
    Refuses(dir, L"\"https://example.com/huggingface.co\" " + file, reason);
    Refuses(dir, L"\"https://user:secret@huggingface.co/x\" " + file, reason);      /* a different address in disguise */
    Refuses(dir, L"\"https://huggingface.co:8443/x\" " + file, reason);             /* another port */
    Refuses(dir, L"\"not an address\" " + file, reason);
}

TEST(RealFetcher_OnlyANewPartFileIsWritten)
{
    if (!FetcherExists())
        SKIP("audior_fetch.exe is not next to run_tests.exe (build.bat)");
    TempDir dir;
    const wchar_t *reason = L"full path and end in .part";
    Refuses(dir, L"\"" + kModel + L"\" \"" + dir.Path(L"model.bin") + L"\" 885098", reason);       /* any other name */
    Refuses(dir, L"\"" + kModel + L"\" \"" + dir.Path(L"model.PART.exe") + L"\" 885098", reason);
    Refuses(dir, L"\"" + kModel + L"\" model.part 885098", reason);                                 /* relative: somewhere unknown */
    Refuses(dir, L"\"" + kModel + L"\" \".part\" 885098", reason);
}

TEST(RealFetcher_TheSizeMustBeOneAModelCanHave)
{
    if (!FetcherExists())
        SKIP("audior_fetch.exe is not next to run_tests.exe (build.bat)");
    TempDir dir;
    const std::wstring where = L"\"" + kModel + L"\" \"" + dir.Path(L"a.part") + L"\" ";
    Refuses(dir, where + L"0", L"size is not one a model can have");
    Refuses(dir, where + L"4294967297", L"size is not one a model can have");       /* more than 4 GiB */
    Refuses(dir, where + L"99999999999999999999", L"size is not one a model can have");
    Refuses(dir, where + L"12ab", L"not a number");
    Refuses(dir, where + L"-5", L"not a number");
    Refuses(dir, where + L"+5", L"not a number");
    Refuses(dir, where + L"\"\"", L"not a number");
}

TEST(RealFetcher_ItWantsExactlyThreeArguments)
{
    if (!FetcherExists())
        SKIP("audior_fetch.exe is not next to run_tests.exe (build.bat)");
    TempDir dir;
    Refuses(dir, L"", L"usage");
    Refuses(dir, L"\"" + kModel + L"\"", L"usage");
    Refuses(dir, L"\"" + kModel + L"\" \"" + dir.Path(L"a.part") + L"\" 100 extra", L"usage");
}
