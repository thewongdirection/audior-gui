#include "setup_plan.h"

#include "format.h"

namespace audiorgui {

namespace {

constexpr std::uint64_t kOwnMemoryBytes = 1ull << 30;       /* a card with less than this shares the PC's memory */

}  // namespace

GpuVerdict JudgeGpu(const HardwareProfile &hw, bool backendPresent)
{
    const GpuInfo *best = nullptr;
    for (const GpuInfo &gpu : hw.gpus) {
        if (!best || gpu.memoryBytes > best->memoryBytes)
            best = &gpu;
    }

    GpuVerdict verdict;
    if (!best)
        return verdict;                                     /* NoCard */
    verdict.card = best->name;
    verdict.cardBytes = best->memoryBytes;
    verdict.use = !backendPresent ? GpuUse::NoBackend : !hw.vulkanDriver ? GpuUse::NoDriver : GpuUse::Usable;
    return verdict;
}

std::wstring DescribeGpu(const GpuVerdict &gpu)
{
    switch (gpu.use) {
    case GpuUse::Usable:
        return gpu.card + (gpu.cardBytes >= kOwnMemoryBytes ? L" (" + FormatBytes(gpu.cardBytes) + L")" : std::wstring()) +
               L" will transcribe.";
    case GpuUse::NoDriver:
        return gpu.card + L" has no Vulkan driver, so the processor will transcribe.";
    case GpuUse::NoBackend:
        return gpu.card + L" was found, but this transcription engine was built without graphics-card support, so "
                          L"the processor will transcribe.";
    default:
        return L"No graphics card was found, so the processor will transcribe.";
    }
}

SetupPlan PlanSetup(const HardwareProfile &hw, bool backendPresent, const std::vector<const AsrModel *> &installed,
                    bool vadInstalled, const AsrModel *models, std::size_t modelCount, const AsrModel *vad)
{
    SetupPlan plan;
    plan.gpu = JudgeGpu(hw, backendPresent);
    if (!installed.empty())
        return plan;

    for (std::size_t i = 0; i < modelCount; ++i) {
        const AsrModel &m = models[i];
        const bool onCard = plan.gpu.use == GpuUse::Usable;
        const bool fits = m.gpuMemoryBytes == 0 || (onCard && plan.gpu.cardBytes >= m.gpuMemoryBytes);
        const bool biggerForThisCard = m.gpuMemoryBelowBytes != 0 && onCard && plan.gpu.cardBytes >= m.gpuMemoryBelowBytes;
        if (m.offered && fits && !biggerForThisCard)
            plan.download.push_back(&m);
    }
    if (!plan.download.empty() && vad && !vadInstalled)
        plan.download.push_back(vad);
    for (const AsrModel *m : plan.download)
        plan.bytes += m->bytes;
    return plan;
}

}  // namespace audiorgui
