/* Tests of the start-up set-up wired to the real hardware scan, folders and preferences, in scratch
 * folders with stand-in files.  Nothing here reaches the network: the one download program is a stand-in. */
#include <chrono>
#include <string>
#include <thread>

#include "../src/core/sha256.h"
#include "../src/engine/asr_helper.h"
#include "../src/engine/system_setup.h"
#include "temp_dir.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

void Write(const std::wstring &path, const std::string &content)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(h);
}

/* A program folder with stand-ins for the helper and its list; `withFetcher` adds the download program. */
void Install(const TempDir &programs, bool withFetcher)
{
    const std::string helper = "MZ pretend helper", fetcher = "MZ pretend download program";
    Write(programs.Path(kHelperFile), helper);
    Write(programs.Path(kManifestFile), Sha256Hex(helper.data(), helper.size()) + "  audior_asr.exe\n");
    if (withFetcher) {
        Write(programs.Path(kFetcherFile), fetcher);
        Write(programs.Path(kFetcherManifest), Sha256Hex(fetcher.data(), fetcher.size()) + "  audior_fetch.exe\n");
    }
}

template <typename Pred>
bool WaitFor(ISetupSource &setup, SetupStatus *last, Pred done)
{
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < end) {
        setup.Poll(last);
        if (done(*last))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool Done(const SetupStatus &s) { return s.phase == SetupPhase::Finished || (s.scanned && s.phase == SetupPhase::Idle); }

}  // namespace

TEST(SystemSetup_ThereIsNothingToSetUpWithoutTheHelper)
{
    TempDir programs, models;
    CHECK(MakeSystemSetup(programs.Dir(), models.Dir(), false) == nullptr);
    CHECK(MakeSystemSetup(std::wstring(), models.Dir(), false) == nullptr);
}

TEST(SystemSetup_WithoutADownloadProgramAMissingModelIsExplained)
{
    TempDir programs, models;
    Install(programs, false);
    const std::unique_ptr<TranscriptionSetup> setup = MakeSystemSetup(programs.Dir(), models.Dir(), false);
    CHECK(setup != nullptr);
    if (!setup)
        return;
    setup->Start();
    SetupStatus s;
    CHECK(WaitFor(*setup, &s, Done));
    CHECK(s.phase == SetupPhase::Finished && s.result == SetupResult::Skipped);     /* no question: it could not be acted on */
    CHECK(s.detail.find(kFetcherFile) != std::wstring::npos);
    CHECK(s.detail.find(models.Dir()) != std::wstring::npos);                       /* where to put a model by hand */
    CHECK(s.canRetry && s.scanned);
    CHECK(!s.declined);                                                             /* and that is not the user's "no" */
}

TEST(SystemSetup_ADownloadProgramThatIsNotTheListedOneIsNotOffered)
{
    TempDir programs, models;
    Install(programs, true);
    Write(programs.Path(kFetcherFile), "MZ a planted program");
    const std::unique_ptr<TranscriptionSetup> setup = MakeSystemSetup(programs.Dir(), models.Dir(), false);
    CHECK(setup != nullptr);
    if (!setup)
        return;
    setup->Start();
    SetupStatus s;
    CHECK(WaitFor(*setup, &s, Done));
    CHECK(s.phase == SetupPhase::Finished && s.result == SetupResult::Skipped);     /* never "Download?" for a program that is not vouched for */
    CHECK(s.detail.find(L"audior_fetch.exe") != std::wstring::npos);
}

TEST(SystemSetup_ANoIsHandedBackForTheProgramToKeep)
{
    TempDir programs, models;
    Install(programs, true);
    SetupStatus s;
    {
        const std::unique_ptr<TranscriptionSetup> setup = MakeSystemSetup(programs.Dir(), models.Dir(), false);
        CHECK(setup != nullptr);
        if (!setup)
            return;
        setup->Start();
        CHECK(WaitFor(*setup, &s, [](const SetupStatus &x) { return x.phase == SetupPhase::Asking; }));
        CHECK(s.detail.find(L"huggingface.co") != std::wstring::npos);
        CHECK(!s.declined);
        setup->Answer(false);
        CHECK(WaitFor(*setup, &s, Done));
        CHECK(s.result == SetupResult::Skipped && s.canRetry);
        CHECK(s.declined);                                              /* what the settings file keeps (SavedSettings::downloadDeclined) */
    }

    {
        const std::unique_ptr<TranscriptionSetup> later = MakeSystemSetup(programs.Dir(), models.Dir(), s.declined);
        CHECK(later != nullptr);
        if (!later)
            return;
        later->Start();                                                 /* the next start of the program */
        SetupStatus t;
        CHECK(WaitFor(*later, &t, Done));
        CHECK(t.phase == SetupPhase::Idle && t.canRetry && t.declined); /* no question this time ... */

        later->Retry();                                                 /* ... until the user asks for it */
        CHECK(WaitFor(*later, &t, [](const SetupStatus &x) { return x.phase == SetupPhase::Asking; }));
    }
}
