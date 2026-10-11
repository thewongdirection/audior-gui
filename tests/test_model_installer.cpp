/* Tests of ModelInstaller with a real child process: run_tests.exe playing the download program (fake_fetch.h). */
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../src/core/sha256.h"
#include "../src/engine/model_installer.h"
#include "fake_fetch.h"
#include "temp_dir.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

constexpr std::size_t kBytes = 100 * 1024;          /* the size of the stand-in "model" */

std::wstring ThisExe()
{
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::wstring(path, n);
}

/* A models folder, a stand-in model with its real size and hash, and an installer whose download program
 * is this very program in the given mode. */
struct Case {
    TempDir     models;
    std::string content = FakeFetchContent(kBytes), hash = Sha256Hex(content.data(), content.size());
    AsrModel    model;

    Case()
    {
        model = {L"tiny.bin", nullptr, kBytes, L"Tiny", L"MIT", L"https://huggingface.co/test/repo"};
        model.sha256 = hash.c_str();
    }

    ModelInstaller::Options Options(const char *mode) const
    {
        const std::string narrow = mode;
        ModelInstaller::Options options;
        options.modelsDir = models.Dir();
        options.fetcher = ThisExe();
        options.fetcherArguments = std::wstring(L"--fake-fetch ") + std::wstring(narrow.begin(), narrow.end());
        return options;
    }

    bool Install(const char *mode, std::wstring *problem, std::vector<std::uint64_t> *downloaded = nullptr,
                 int *checked = nullptr) const
    {
        ModelInstaller installer(Options(mode));
        return installer.Install(
            model,
            [&](ModelInstaller::Stage stage, std::uint64_t bytes) {
                if (stage == ModelInstaller::Stage::Downloading && downloaded)
                    downloaded->push_back(bytes);
                if (stage == ModelInstaller::Stage::Checking && checked)
                    ++*checked;
            },
            [] { return false; }, problem);
    }
};

template <typename Pred>
bool WaitUntil(Pred done, int timeoutMs = 10000)
{
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
}

bool Has(const std::wstring &text, const wchar_t *part) { return text.find(part) != std::wstring::npos; }

}  // namespace

TEST(Install_ADownloadIsCheckedAndMovedIntoPlace)
{
    Case c;
    std::wstring problem;
    std::vector<std::uint64_t> downloaded;
    int checked = 0;
    CHECK(c.Install("ok", &problem, &downloaded, &checked));
    CHECK(problem.empty());

    const std::vector<std::uint8_t> bytes = ReadFileBytes(c.models.Path(L"tiny.bin"));
    CHECK_EQ(bytes.size(), kBytes);
    CHECK_EQ(Sha256Hex(bytes.data(), bytes.size()), c.hash);
    CHECK_EQ(c.models.Names().size(), std::size_t(1));          /* no .part file is left */

    CHECK(downloaded.size() > 1);                               /* it was told how far it had got, more than once ... */
    CHECK_EQ(downloaded.back(), std::uint64_t(kBytes));
    for (std::size_t i = 1; i < downloaded.size(); ++i)
        CHECK(downloaded[i] >= downloaded[i - 1]);              /* ... and never backwards */
    CHECK_EQ(checked, 1);                                       /* then that it was checking */
}

TEST(Install_ItIsGivenTheAddressOfTheFileAndWhereToPutIt)
{
    Case c;
    std::wstring problem;
    CHECK(c.Install("ok", &problem));                           /* "ok" refuses anything but https://huggingface.co/.../resolve/main/<name>, a .part file and a size */
    CHECK(problem.empty());
}

TEST(Install_AFileOfTheWrongSizeIsRefusedWhateverTheProgramSays)
{
    Case c;
    std::wstring problem;
    CHECK(!c.Install("short", &problem));
    CHECK(Has(problem, L"size") && Has(problem, L"Tiny"));
    CHECK(c.models.Names().empty());
}

TEST(Install_AFileWithOtherContentIsRefused)
{
    Case c;
    std::wstring problem;
    CHECK(!c.Install("corrupt", &problem));
    CHECK(Has(problem, L"SHA-256") && Has(problem, L"Tiny"));
    CHECK(c.models.Names().empty());                            /* neither under its name nor as a .part */
}

TEST(Install_WhatTheProgramReportsIsPassedOn)
{
    Case c;
    std::wstring problem;
    CHECK(!c.Install("error", &problem));
    CHECK(Has(problem, L"no route") && Has(problem, L"Tiny"));
    CHECK(c.models.Names().empty());
}

TEST(Install_AProgramThatDiesWithoutAWordIsAFailure)
{
    Case c;
    std::wstring problem;
    CHECK(!c.Install("die", &problem));
    CHECK(Has(problem, L"stopped"));
    CHECK(c.models.Names().empty());
}

TEST(Install_AProgramThatCannotBeStartedIsAFailure)
{
    Case c;
    ModelInstaller::Options options = c.Options("ok");
    options.fetcher = c.models.Path(L"missing.exe");
    ModelInstaller installer(options);
    std::wstring problem;
    CHECK(!installer.Install(c.model, nullptr, [] { return false; }, &problem));
    CHECK(Has(problem, L"could not be started"));
    CHECK(c.models.Names().empty());
}

TEST(Install_AProgramThatMustNotRunIsNotStarted)
{
    Case c;
    ModelInstaller::Options options = c.Options("ok");
    options.verify = [] { return std::wstring(L"The download program is not the one in its file list."); };
    ModelInstaller installer(options);
    std::wstring problem;
    CHECK(!installer.Install(c.model, nullptr, [] { return false; }, &problem));
    CHECK_EQ(problem, std::wstring(L"The download program is not the one in its file list."));
    CHECK(c.models.Names().empty());                            /* it never ran, so nothing was written */
}

TEST(Install_AskedToStopBeforeItBeginsItDoesNothing)
{
    Case c;
    ModelInstaller::Options options = c.Options("ok");
    options.fetcher = c.models.Path(L"never-started.exe");
    ModelInstaller installer(options);
    std::wstring problem;
    CHECK(!installer.Install(c.model, nullptr, [] { return true; }, &problem));
    CHECK(Has(problem, L"cancelled"));
}

TEST(Install_CancelEndsADownloadThatIsWaiting)
{
    Case c;
    ModelInstaller installer(c.Options("hang"));
    std::atomic<bool> stop{false}, started{false}, done{false};
    std::wstring problem;
    bool ok = true;
    std::thread worker([&] {
        ok = installer.Install(c.model, [&](ModelInstaller::Stage, std::uint64_t bytes) { started = started || bytes > 0; },
                               [&] { return stop.load(); }, &problem);
        done = true;
    });
    CHECK(WaitUntil([&] { return started.load(); }));           /* the program is up and silent now */
    stop = true;
    for (int i = 0; i < 200 && !done; ++i) {                    /* (repeated, in case the first call came too early) */
        installer.Cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    worker.join();
    CHECK(!ok);
    CHECK(Has(problem, L"cancelled"));
    CHECK(c.models.Names().empty());
}

TEST(Install_ALeftoverPartAndAFileOfTheWrongSizeAreReplaced)
{
    Case c;
    HANDLE a = CreateFileW(c.models.Path(L"tiny.bin.part").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    HANDLE b = CreateFileW(c.models.Path(L"tiny.bin").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n = 0;
    WriteFile(a, "junk", 4, &n, nullptr);
    WriteFile(b, "junk", 4, &n, nullptr);
    CloseHandle(a);
    CloseHandle(b);

    std::wstring problem;
    CHECK(c.Install("ok", &problem));
    CHECK_EQ(ReadFileBytes(c.models.Path(L"tiny.bin")).size(), kBytes);
    CHECK_EQ(c.models.Names().size(), std::size_t(1));
}

TEST(Install_ThePathToTheFolderIsCreated)
{
    Case c;
    ModelInstaller::Options options = c.Options("ok");
    options.modelsDir = c.models.Path(L"one\\two");
    ModelInstaller installer(options);
    std::wstring problem;
    CHECK(installer.Install(c.model, nullptr, [] { return false; }, &problem));
    CHECK_EQ(ReadFileBytes(c.models.Path(L"one\\two\\tiny.bin")).size(), kBytes);
}

TEST(Install_NoFolderMeansNoDownload)
{
    Case c;
    ModelInstaller::Options options = c.Options("ok");
    options.modelsDir.clear();
    ModelInstaller installer(options);
    std::wstring problem;
    CHECK(!installer.Install(c.model, nullptr, [] { return false; }, &problem));
    CHECK(Has(problem, L"folder"));
}

TEST(Install_AFolderThatCouldBreakTheArgumentsIsRefused)
{
    Case c;
    ModelInstaller::Options options = c.Options("ok");
    options.modelsDir = c.models.Path(L"a\" b");                   /* a quote in a path, which Windows would never make */
    ModelInstaller installer(options);
    std::wstring problem;
    CHECK(!installer.Install(c.model, nullptr, [] { return false; }, &problem));
    CHECK(Has(problem, L"not a valid path"));
    CHECK(c.models.Names().empty());
}
