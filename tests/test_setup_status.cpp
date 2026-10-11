/* Tests of the words of the start-up set-up's notification. */
#include <string>

#include "../src/core/setup_status.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

constexpr std::uint64_t kMiB = 1ull << 20;
const char kHash[] = "0000000000000000000000000000000000000000000000000000000000000000";

const AsrModel kSmall = {L"small.bin", kHash, 100 * kMiB, L"Small", L"MIT", L"https://example.test/m"};
const AsrModel kBig   = {L"big.bin", kHash, 300 * kMiB, L"Big", L"MIT", L"https://example.test/m", true};
const AsrModel kVoice = {L"voice.bin", kHash, 1 * kMiB, L"Voice", L"MIT", L"https://example.test/v"};

SetupPlan ThreeFiles()
{
    SetupPlan plan;
    plan.gpu.use = GpuUse::NoCard;
    plan.download = {&kSmall, &kBig, &kVoice};
    plan.bytes = 401 * kMiB;
    return plan;
}

bool Has(const std::wstring &text, const wchar_t *part) { return text.find(part) != std::wstring::npos; }

}  // namespace

TEST(Status_ScanningIsAMessageWithNoProgress)
{
    const SetupStatus s = ScanningStatus();
    CHECK(s.phase == SetupPhase::Scanning);
    CHECK(s.result == SetupResult::None);
    CHECK(!s.title.empty() && !s.detail.empty());
    CHECK(s.fraction < 0.0f);
}

TEST(Status_TheQuestionNamesTheFilesTheirSizeAndWhereTheyComeFrom)
{
    const SetupStatus s = AskingStatus(ThreeFiles());
    CHECK(s.phase == SetupPhase::Asking);
    CHECK(Has(s.detail, L"Small, Big and Voice"));
    CHECK(Has(s.detail, L"401.0 MB"));
    CHECK(Has(s.detail, L"huggingface.co"));
    CHECK(Has(s.detail, L"processor"));                                 /* and what the scan found out */
}

TEST(Status_AQuestionAboutOneFileNamesJustThat)
{
    SetupPlan plan;
    plan.download = {&kSmall};
    plan.bytes = kSmall.bytes;
    CHECK(Has(AskingStatus(plan).detail, L"Download Small ("));
}

TEST(Status_ADownloadSaysWhichFileHowMuchAndHowFarItIs)
{
    const SetupPlan plan = ThreeFiles();
    const SetupStatus s = DownloadingStatus(plan, 1, 200 * kMiB);
    CHECK(s.phase == SetupPhase::Downloading);
    CHECK(Has(s.title, L"Big") && Has(s.title, L"2 of 3"));
    CHECK(Has(s.detail, L"200.0 MB of 401.0 MB") && Has(s.detail, L"49%"));
    CHECK_NEAR(s.fraction, 200.0 / 401.0, 1e-6);

    CHECK(!Has(DownloadingStatus([&] { SetupPlan one; one.download = {&kSmall}; one.bytes = kSmall.bytes; return one; }(), 0, 0).title, L" of "));
    CHECK_NEAR(DownloadingStatus(plan, 0, 0).fraction, 0.0, 1e-9);
}

TEST(Status_ADownloadNeverShowsMoreThanTheWhole)
{
    const SetupStatus s = DownloadingStatus(ThreeFiles(), 2, 10000 * kMiB);
    CHECK_NEAR(s.fraction, 1.0, 1e-9);
    CHECK(Has(s.detail, L"100%"));
}

TEST(Status_CheckingAFileHasNoProgressBar)
{
    const SetupStatus s = CheckingStatus(ThreeFiles(), 0);
    CHECK(s.phase == SetupPhase::Downloading);
    CHECK(Has(s.title, L"Small"));
    CHECK(s.fraction < 0.0f);
}

TEST(Status_ReadyNamesTheModelAndTheCard)
{
    GpuVerdict gpu;
    gpu.use = GpuUse::Usable;
    gpu.card = L"Test Card";
    const SetupStatus s = ReadyStatus(gpu, L"Small", false);
    CHECK(s.phase == SetupPhase::Finished && s.result == SetupResult::Ready);
    CHECK(Has(s.detail, L"Test Card") && Has(s.detail, L"Model: Small."));
    CHECK(ReadyStatus(gpu, L"Small", true).title != s.title);           /* a download is said to have happened */
}

TEST(Status_SkippedAndFailedAreFinishedWithTheirOwnWords)
{
    const SetupStatus skipped = SkippedStatus(false), cancelled = SkippedStatus(true);
    CHECK(skipped.phase == SetupPhase::Finished && skipped.result == SetupResult::Skipped);
    CHECK(skipped.title != cancelled.title);
    CHECK(Has(skipped.detail, L"Transcript section"));

    const SetupStatus failed = FailedStatus(L"Download failed", L"No connection.");
    CHECK(failed.phase == SetupPhase::Finished && failed.result == SetupResult::Failed);
    CHECK_EQ(failed.title, std::wstring(L"Download failed"));
    CHECK_EQ(failed.detail, std::wstring(L"No connection."));
}
