/* Tests of the start-up set-up's conversation with the user, with made-up hardware, models and downloads. */
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "../src/engine/transcription_setup.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

constexpr std::uint64_t kMiB = 1ull << 20;
const char kHash[] = "0000000000000000000000000000000000000000000000000000000000000000";

const AsrModel kTable[] = {
    {L"small.bin", kHash, 100 * kMiB, L"Small", L"MIT", L"https://example.test/m"},
    {L"big.bin", kHash, 300 * kMiB, L"Big", L"MIT", L"https://example.test/m", true, 4ull << 30},
};
const AsrModel kVoice = {L"voice.bin", kHash, 1 * kMiB, L"Voice", L"MIT", L"https://example.test/v"};

bool Has(const std::wstring &text, const wchar_t *part) { return text.find(part) != std::wstring::npos; }

/* The world the set-up looks at and acts on. */
struct World {
    HardwareProfile               hardware;
    bool                          backend = true;
    std::vector<const AsrModel *> installed;
    bool                          voice = false;
    std::wstring                  cannotDownload;
    bool                          declined = false;

    std::vector<std::wstring> downloads;                                /* the files asked for, in order */
    std::atomic<int>          declinedWrites{0};
    std::wstring              failWith;                                 /* a download of this file fails ... */
    std::wstring              failReason;
    std::wstring              blockFile;                                /* ... and one of this file waits until it is cancelled */
    std::wstring              cancelDuring;                             /* ... and the user cancels in the instant this file arrives */
    TranscriptionSetup       *setup = nullptr;                          /* (for that) */
    std::atomic<bool>         holdScan{false};                          /* the scan of the hardware takes as long as this is set */
    std::atomic<int>          scans{0};
    std::atomic<bool>         installWaiting{false};
    int                       installMs = 0;                            /* each download takes this long */
    std::atomic<bool>         cancelCalled{false};

    World()
    {
        hardware.vulkanDriver = true;
        hardware.gpus.push_back({L"Test Card", 8ull << 30});
    }

    TranscriptionSetup::Environment Env()
    {
        TranscriptionSetup::Environment env;
        env.scan = [this] {
            ++scans;
            while (holdScan)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return hardware;
        };
        env.backendPresent = backend;
        env.installedModels = [this] { return installed; };
        env.voiceModelInstalled = [this] { return voice; };
        env.downloadProblem = [this] { return cannotDownload; };
        env.install = [this](const AsrModel &model, const ModelInstaller::Progress &progress, const StopCheck &stopping,
                             std::wstring *problem) {
            downloads.push_back(model.file);
            progress(ModelInstaller::Stage::Downloading, model.bytes / 2);
            std::this_thread::sleep_for(std::chrono::milliseconds(installMs));
            if (model.file == blockFile) {
                installWaiting = true;
                while (!stopping())
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                *problem = L"cancelled";
                return false;
            }
            if (model.file == failWith) {
                *problem = failReason;
                return false;
            }
            progress(ModelInstaller::Stage::Checking, 0);
            if (model.file == cancelDuring && setup)
                setup->Cancel();                                        /* it is whole all the same: this install is done */
            if (&model >= kTable && &model < kTable + 2)
                installed.push_back(&model);                            /* what a download does to the folder */
            else
                voice = true;
            return true;
        };
        env.cancelInstall = [this] { cancelCalled = true; };
        env.declined = [this] { return declined; };
        env.rememberDecline = [this](bool d) {
            declined = d;
            ++declinedWrites;
        };
        env.models = kTable;
        env.modelCount = 2;
        env.voice = &kVoice;
        return env;
    }
};

/* Polls `setup` until `done(status)` or too long; the last status seen is left in *last. */
template <typename Pred>
bool WaitFor(TranscriptionSetup &setup, SetupStatus *last, Pred done, int timeoutMs = 10000)
{
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        setup.Poll(last);
        if (done(*last))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    setup.Poll(last);
    return done(*last);
}

bool Finished(const SetupStatus &s) { return s.phase == SetupPhase::Finished || (s.scanned && s.phase == SetupPhase::Idle); }
bool Asking(const SetupStatus &s) { return s.phase == SetupPhase::Asking; }
bool Downloading(const SetupStatus &s) { return s.phase == SetupPhase::Downloading; }

}  // namespace

TEST(Setup_AModelThatIsThereMeansNothingToAskAndTheCardIsNamed)
{
    World world;
    world.installed = {&kTable[0]};
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(s.phase == SetupPhase::Finished && s.result == SetupResult::Ready);
    CHECK(Has(s.detail, L"Test Card") && Has(s.detail, L"Small"));
    CHECK(s.scanned && s.gpuUsable);
    CHECK(!s.modelsChanged && !s.canRetry);
    CHECK(world.downloads.empty());
}

TEST(Setup_NoUsableCardTurnsTheGpuOff)
{
    for (int why = 0; why < 3; ++why) {
        World world;
        world.installed = {&kTable[0]};
        if (why == 0)
            world.hardware.gpus.clear();                                /* no card */
        else if (why == 1)
            world.hardware.vulkanDriver = false;                        /* no driver */
        else
            world.backend = false;                                      /* a helper without the GPU backend */
        TranscriptionSetup setup(world.Env());
        setup.Start();
        SetupStatus s;
        CHECK(WaitFor(setup, &s, Finished));
        CHECK(s.scanned);
        CHECK(!s.gpuUsable);
        CHECK(Has(s.detail, L"processor"));
    }
}

TEST(Setup_NothingIsDownloadedUntilTheUserSaysYes)
{
    World world;
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    CHECK(s.scanned && s.gpuUsable);                                    /* the scan is done before the question */
    CHECK(Has(s.detail, L"Small, Big and Voice") && Has(s.detail, L"401.0 MB"));       /* a card with 8 GiB: the big one too */
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(world.downloads.empty());
    CHECK(!setup.Poll(&s));                                             /* and it just waits: nothing changes */
    CHECK_EQ(world.declinedWrites.load(), 0);
}

TEST(Setup_AYesDownloadsEveryFileInOrderAndSaysSo)
{
    World world;
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    CHECK(WaitFor(setup, &s, Finished));

    CHECK_EQ(world.downloads.size(), std::size_t(3));
    CHECK_EQ(world.downloads[0], std::wstring(L"small.bin"));
    CHECK_EQ(world.downloads[1], std::wstring(L"big.bin"));
    CHECK_EQ(world.downloads[2], std::wstring(L"voice.bin"));
    CHECK(s.result == SetupResult::Ready && s.modelsChanged && !s.canRetry);
    CHECK(Has(s.title, L"installed") && Has(s.detail, L"Model: Small."));
    CHECK(s.scanned && s.gpuUsable);
    CHECK(!world.declined);                                             /* a yes clears an earlier no */
}

TEST(Setup_AYesIsShownAsProgressThatNeverGoesBack)
{
    World world;
    world.installMs = 20;                                               /* long enough for the polls to see each step */
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);

    float last = -1.0f;
    bool sawDownloading = false;
    for (int i = 0; i < 5000 && !Finished(s); ++i) {
        if (setup.Poll(&s) && Downloading(s) && s.fraction >= 0.0f) {
            sawDownloading = true;
            CHECK(s.fraction >= last - 1e-6f);
            last = s.fraction;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(Finished(s));
    CHECK(sawDownloading);                                              /* (the polls may skip some, never reorder) */
}

TEST(Setup_ANoIsRememberedAndNothingIsDownloaded)
{
    World world;
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(false);
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(s.result == SetupResult::Skipped);
    CHECK(Has(s.title, L"skipped"));
    CHECK(s.canRetry && !s.modelsChanged);
    CHECK(world.declined);
    CHECK(world.downloads.empty());
}

TEST(Setup_ARememberedNoMeansNoNotificationAndNoQuestion)
{
    World world;
    world.declined = true;
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(s.phase == SetupPhase::Idle);                                 /* nothing to show ... */
    CHECK(s.scanned && s.canRetry);                                     /* ... but the Transcript section can offer it again */
    CHECK(world.downloads.empty());
}

TEST(Setup_RetryAsksAgainEvenAfterANo)
{
    World world;
    world.declined = true;
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Finished));

    setup.Retry();
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    CHECK(WaitFor(setup, &s, [](const SetupStatus &x) { return x.phase == SetupPhase::Finished; }));
    CHECK(s.result == SetupResult::Ready && s.modelsChanged);
    CHECK(!world.declined);
    CHECK_EQ(world.downloads.size(), std::size_t(3));
}

TEST(Setup_AFailureSaysWhyAndIsNotRememberedAsANo)
{
    World world;
    world.failWith = L"small.bin";
    world.failReason = L"No connection.";
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(s.result == SetupResult::Failed);
    CHECK_EQ(s.detail, std::wstring(L"No connection."));
    CHECK(s.canRetry && !s.modelsChanged);
    CHECK(!world.declined);                                             /* it will be asked again at the next start */
    CHECK_EQ(world.downloads.size(), std::size_t(1));                   /* and the rest is not tried */

    world.failWith.clear();                                             /* the network is back */
    setup.Retry();
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    CHECK(WaitFor(setup, &s, [](const SetupStatus &x) { return x.phase == SetupPhase::Finished; }));
    CHECK(s.result == SetupResult::Ready);
}

TEST(Setup_WhatArrivedBeforeAFailureIsKept)
{
    World world;
    world.failWith = L"big.bin";
    world.failReason = L"The connection was lost.";
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(s.result == SetupResult::Failed);
    CHECK(s.modelsChanged);                                             /* the small model is there: look again */
    CHECK(!s.canRetry);                                                 /* and with one installed there is nothing to insist on */
}

TEST(Setup_CancelEndsTheDownloadAndKeepsWhatIsWhole)
{
    World world;
    world.blockFile = L"big.bin";                                       /* the small model arrives, the big one waits for the network */
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    for (int i = 0; i < 5000 && !world.installWaiting; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(world.installWaiting);
    CHECK(WaitFor(setup, &s, Downloading));
    CHECK(Has(s.title, L"Big") && Has(s.title, L"2 of 3"));

    setup.Cancel();
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(world.cancelCalled);
    CHECK(s.result == SetupResult::Skipped && Has(s.title, L"cancelled"));
    CHECK(s.modelsChanged);                                             /* the small one is whole: the recorder looks again */
    CHECK(!s.canRetry);
    CHECK(!world.declined);                                             /* a yes, then second thoughts: not a "no" for good */
    CHECK_EQ(world.downloads.size(), std::size_t(2));                   /* and the third file is not started */
}

TEST(Setup_CancelBeforeAnythingIsWholeOffersTheDownloadAgain)
{
    World world;
    world.blockFile = L"small.bin";
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    for (int i = 0; i < 5000 && !world.installWaiting; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(WaitFor(setup, &s, Downloading));
    setup.Cancel();
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(!s.modelsChanged && s.canRetry);
}

TEST(Setup_AnAnswerThatIsNotAnswerToAQuestionIsIgnored)
{
    World world;
    world.installed = {&kTable[0]};
    TranscriptionSetup setup(world.Env());
    setup.Answer(true);                                                 /* before anything */
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Finished));
    setup.Answer(true);                                                 /* after everything */
    setup.Cancel();
    CHECK(!setup.Poll(&s));                                             /* neither did anything */
    CHECK(world.downloads.empty());
    CHECK_EQ(world.declinedWrites.load(), 0);
}

TEST(Setup_RetryWithNothingMissingJustChecksAgain)
{
    World world;
    world.installed = {&kTable[0]};
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Finished));

    setup.Retry();
    bool changed = false;
    for (int i = 0; i < 5000 && !(changed && Finished(s)); ++i) {
        changed = setup.Poll(&s) || changed;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(changed && s.result == SetupResult::Ready);
    CHECK(world.downloads.empty());
}

TEST(Setup_AMissingDownloadProgramMeansNoQuestionAndSaysWhatToDo)
{
    World world;
    world.cannotDownload = L"The download program is not here.";
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(s.result == SetupResult::Skipped);
    CHECK(Has(s.title, L"No speech model") && s.detail == world.cannotDownload);
    CHECK(s.canRetry && s.scanned);
    CHECK(world.downloads.empty());
    CHECK_EQ(world.declinedWrites.load(), 0);
}

TEST(Setup_APcWithAWeakCardIsOnlyOfferedTheSmallModel)
{
    World world;
    world.hardware.gpus[0].memoryBytes = 2ull << 30;
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    CHECK(Has(s.detail, L"Small and Voice") && !Has(s.detail, L"Big"));
    setup.Answer(true);
    CHECK(WaitFor(setup, &s, [](const SetupStatus &x) { return x.phase == SetupPhase::Finished; }));
    CHECK_EQ(world.downloads.size(), std::size_t(2));
}

TEST(Setup_ClosingWhileAskingDoesNotHang)
{
    World world;
    std::atomic<bool> closed{false};
    std::thread closer([&] {
        {
            TranscriptionSetup setup(world.Env());
            setup.Start();
            SetupStatus s;
            WaitFor(setup, &s, Asking);
        }                                                               /* destroyed with the question unanswered */
        closed = true;
    });
    for (int i = 0; i < 5000 && !closed; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(closed);
    if (closed)
        closer.join();
    else
        closer.detach();                                                /* (a hang is a failure, not a stuck test run) */
    CHECK(world.downloads.empty());
}

TEST(Setup_ClosingWhileDownloadingEndsTheDownload)
{
    World world;
    world.blockFile = L"small.bin";
    std::atomic<bool> closed{false};
    std::thread closer([&] {
        {
            TranscriptionSetup setup(world.Env());
            setup.Start();
            SetupStatus s;
            WaitFor(setup, &s, Asking);
            setup.Answer(true);
            for (int i = 0; i < 5000 && !world.installWaiting; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }                                                               /* destroyed in the middle of a download */
        closed = true;
    });
    for (int i = 0; i < 5000 && !closed; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(closed);
    CHECK(world.cancelCalled);
    if (closed)
        closer.join();
    else
        closer.detach();
}

TEST(Setup_PollReportsAChangeOnlyOnce)
{
    World world;
    world.installed = {&kTable[0]};
    TranscriptionSetup setup(world.Env());
    SetupStatus s;
    CHECK(!setup.Poll(&s));                                             /* nothing has happened yet */
    setup.Start();
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(!setup.Poll(&s));                                             /* the same status is not reported twice */
}

TEST(Setup_ACancelThatComesAsTheLastFileArrivesChangesNothing)
{
    World world;
    world.cancelDuring = L"voice.bin";                                  /* the last file of the plan */
    TranscriptionSetup setup(world.Env());
    world.setup = &setup;
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(s.result == SetupResult::Ready);                              /* everything is in place: not "cancelled" ... */
    CHECK(s.modelsChanged);                                             /* ... and the recorder is told to look */
    CHECK_EQ(world.downloads.size(), std::size_t(3));
}

TEST(Setup_ACancelBetweenTwoFilesStopsBeforeTheNextOne)
{
    World world;
    world.cancelDuring = L"small.bin";                                  /* arrives whole, then the user cancels */
    TranscriptionSetup setup(world.Env());
    world.setup = &setup;
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    CHECK(WaitFor(setup, &s, Finished));
    CHECK(s.result == SetupResult::Skipped && Has(s.title, L"cancelled"));
    CHECK(s.modelsChanged);                                             /* the first one is in place */
    CHECK_EQ(world.downloads.size(), std::size_t(1));                   /* the next is never started */
}

TEST(Setup_RetryAfterACancelStartsCleanly)
{
    World world;
    world.blockFile = L"small.bin";
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    for (int i = 0; i < 5000 && !world.installWaiting; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(WaitFor(setup, &s, Downloading));
    setup.Cancel();
    CHECK(WaitFor(setup, &s, Finished));

    world.blockFile.clear();                                            /* a better connection */
    setup.Retry();
    CHECK(WaitFor(setup, &s, Asking));
    setup.Answer(true);
    CHECK(WaitFor(setup, &s, [](const SetupStatus &x) { return x.phase == SetupPhase::Finished; }));
    CHECK(s.result == SetupResult::Ready && s.modelsChanged);          /* the old cancel is not remembered */
}

TEST(Setup_TheFirstPollAfterStartAlreadyHasSomethingToSay)
{
    World world;
    world.holdScan = true;                                              /* the thread has not got further than starting */
    TranscriptionSetup setup(world.Env());
    setup.Start();
    SetupStatus s;
    CHECK(setup.Poll(&s));                                              /* so a window that looks at once is not left with nothing */
    CHECK(s.phase == SetupPhase::Scanning);
    world.holdScan = false;
    CHECK(WaitFor(setup, &s, Asking));
}

TEST(Setup_RetryIsRefusedWhileARunIsInProgress)
{
    World world;
    world.holdScan = true;
    TranscriptionSetup setup(world.Env());
    setup.Start();
    setup.Retry();                                                      /* still scanning: no second run */
    world.holdScan = false;
    SetupStatus s;
    CHECK(WaitFor(setup, &s, Asking));
    setup.Retry();                                                      /* asking: not over either */
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_EQ(world.scans.load(), 1);
}

TEST(Setup_ClosingWhileScanningWaitsForTheScanAndNothingElse)
{
    World world;
    world.holdScan = true;
    std::atomic<bool> closed{false};
    std::thread closer([&] {
        {
            TranscriptionSetup setup(world.Env());
            setup.Start();
        }                                                               /* destroyed while the hardware is being looked at */
        closed = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!closed);                                                     /* it cannot cut a scan short ... */
    world.holdScan = false;
    for (int i = 0; i < 5000 && !closed; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(closed);                                                      /* ... but is not held up beyond it */
    if (closed)
        closer.join();
    else
        closer.detach();
    CHECK(world.downloads.empty());
}
