/* Tests of what a scan of the hardware decides: the graphics card, and which models to offer. */
#include <string>
#include <vector>

#include "../src/core/setup_plan.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

constexpr std::uint64_t kGiB = 1ull << 30;
const char kHash[] = "0000000000000000000000000000000000000000000000000000000000000000";

/* A small table in the table's own shape: one model for every PC, one that wants a card of 4 GiB. */
const AsrModel kTable[] = {
    {L"small.bin", kHash, 100, L"Small", L"MIT", L"https://example.test/m"},
    {L"big.bin", kHash, 300, L"Big", L"MIT", L"https://example.test/m", nullptr, 4 * kGiB},
};
const AsrModel &kSmall = kTable[0], &kBig = kTable[1];
const AsrModel kVoice = {L"voice.bin", kHash, 7, L"Voice", L"MIT", L"https://example.test/v"};

HardwareProfile Pc(std::uint64_t cardBytes, bool driver = true)
{
    HardwareProfile hw;
    hw.vulkanDriver = driver;
    if (cardBytes > 0)
        hw.gpus.push_back({L"Test Card", cardBytes});
    return hw;
}

SetupPlan Plan(const HardwareProfile &hw, bool backend = true, std::vector<const AsrModel *> installed = {},
               bool voice = false)
{
    return PlanSetup(hw, backend, installed, voice, kTable, 2, &kVoice);
}

}  // namespace

/* ---- the graphics card ---- */

TEST(Gpu_NoAdapterMeansTheProcessor)
{
    const GpuVerdict v = JudgeGpu(Pc(0), true);
    CHECK(v.use == GpuUse::NoCard);
    CHECK(v.card.empty());
}

TEST(Gpu_ACardNeedsADriverAndAHelperThatCanUseIt)
{
    CHECK(JudgeGpu(Pc(8 * kGiB), true).use == GpuUse::Usable);
    CHECK(JudgeGpu(Pc(8 * kGiB, false), true).use == GpuUse::NoDriver);
    CHECK(JudgeGpu(Pc(8 * kGiB), false).use == GpuUse::NoBackend);
    CHECK(JudgeGpu(Pc(8 * kGiB, false), false).use == GpuUse::NoBackend);       /* the helper is the first thing to lack */
    CHECK_EQ(JudgeGpu(Pc(8 * kGiB), false).card, std::wstring(L"Test Card"));   /* the card is still named */
}

TEST(Gpu_TheCardWithTheMostMemoryIsTheOneUsed)
{
    HardwareProfile hw = Pc(256ull << 20);                              /* an integrated one */
    hw.gpus.push_back({L"Big Card", 12 * kGiB});
    hw.gpus.push_back({L"Other Card", 2 * kGiB});
    const GpuVerdict v = JudgeGpu(hw, true);
    CHECK_EQ(v.card, std::wstring(L"Big Card"));
    CHECK_EQ(v.cardBytes, 12 * kGiB);
}

TEST(Gpu_TheSentenceSaysWhatWillHappen)
{
    CHECK_EQ(DescribeGpu(JudgeGpu(Pc(16 * kGiB), true)), std::wstring(L"Test Card (16.0 GB) will transcribe."));
    CHECK_EQ(DescribeGpu(JudgeGpu(Pc(256ull << 20), true)), std::wstring(L"Test Card will transcribe."));    /* shares the PC's memory: no size */

    const std::wstring none = DescribeGpu(JudgeGpu(Pc(0), true));
    CHECK(none.find(L"No graphics card") != std::wstring::npos && none.find(L"processor") != std::wstring::npos);
    const std::wstring noDriver = DescribeGpu(JudgeGpu(Pc(8 * kGiB, false), true));
    CHECK(noDriver.find(L"Test Card") != std::wstring::npos && noDriver.find(L"Vulkan") != std::wstring::npos &&
          noDriver.find(L"processor") != std::wstring::npos);
    const std::wstring noBackend = DescribeGpu(JudgeGpu(Pc(8 * kGiB), false));
    CHECK(noBackend.find(L"Test Card") != std::wstring::npos && noBackend.find(L"processor") != std::wstring::npos);
}

/* ---- the models ---- */

TEST(Plan_AnyInstalledModelMeansNothingToDownload)
{
    const SetupPlan plan = Plan(Pc(8 * kGiB), true, {&kBig});
    CHECK(plan.download.empty());
    CHECK_EQ(plan.bytes, std::uint64_t(0));
    CHECK(plan.gpu.use == GpuUse::Usable);                              /* the card is judged all the same */
}

TEST(Plan_APcWithoutAGoodCardIsOfferedOnlyWhatSuitsEveryPc)
{
    for (const SetupPlan &plan : {Plan(Pc(0)), Plan(Pc(2 * kGiB)), Plan(Pc(16 * kGiB, false)), Plan(Pc(16 * kGiB), false)}) {
        CHECK_EQ(plan.download.size(), std::size_t(2));
        CHECK(plan.download[0] == &kSmall);
        CHECK(plan.download[1] == &kVoice);                             /* the voice model rides along */
        CHECK_EQ(plan.bytes, std::uint64_t(107));
    }
}

TEST(Plan_ACardThatCanUseItIsOfferedTheBigModelToo)
{
    const SetupPlan plan = Plan(Pc(4 * kGiB));                          /* exactly enough */
    CHECK_EQ(plan.download.size(), std::size_t(3));
    CHECK(plan.download[0] == &kSmall);
    CHECK(plan.download[1] == &kBig);
    CHECK(plan.download[2] == &kVoice);
    CHECK_EQ(plan.bytes, std::uint64_t(407));
}

TEST(Plan_TheVoiceModelOnlyRidesAlong)
{
    CHECK_EQ(Plan(Pc(0), true, {}, true).download.size(), std::size_t(1));          /* installed already */
    CHECK(Plan(Pc(0), true, {&kSmall}, false).download.empty());                    /* never offered alone */
    const SetupPlan noVoice = PlanSetup(Pc(0), true, {}, false, kTable, 2, nullptr);
    CHECK_EQ(noVoice.download.size(), std::size_t(1));
}

TEST(Plan_ThePlanOfTheRealTableAlwaysOffersAModelToAPcWithNone)
{
    const SetupPlan plan = PlanSetup(HardwareProfile(), true, {}, false);
    CHECK(plan.download.size() >= 2);                                   /* a model and the voice model */
    CHECK(plan.download[0]->gpuMemoryBytes == 0);                       /* the one that suits every PC (Parakeet), and no other */
    CHECK(plan.download[0]->engine == AsrEngine::Parakeet);
    CHECK_EQ(plan.download.size(), std::size_t(2));
    CHECK(plan.download.back() == &kVadModel);
    std::uint64_t total = 0;
    for (const AsrModel *m : plan.download) {
        total += m->bytes;
        CHECK(m == &kVadModel || m->offered);                           /* what the user has to bring is never fetched */
    }
    CHECK_EQ(plan.bytes, total);
}

TEST(Plan_AModelThatIsNotOfferedIsNeverPlannedForDownload)
{
    AsrModel table[2] = {{L"a.bin", "x", 100, L"A", L"MIT", L"https://huggingface.co/x"},
                         {L"b.bin", "x", 200, L"B", L"MIT", L"https://huggingface.co/x"}};
    table[1].offered = false;
    const SetupPlan plan = PlanSetup(HardwareProfile(), true, {}, true, table, 2, nullptr);
    CHECK_EQ(plan.download.size(), std::size_t(1));
    CHECK(plan.download.size() == 1 && plan.download[0] == &table[0]);
    CHECK_EQ(plan.bytes, std::uint64_t(100));
}
