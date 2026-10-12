/*
 * setup_plan.h - What a look at this PC decides about live transcription.
 *
 * At start-up the program scans the hardware (the Windows side of that is
 * engine/hardware_scan) and this file turns the result into decisions:
 *
 *   - whether the transcription helper should use the graphics card, and what to
 *     tell the user about it (JudgeGpu);
 *   - which speech models to download, if no model is installed at all (PlanSetup).
 *
 * Plain C++, no Windows, so every rule is unit-tested with made-up hardware.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "asr_models.h"

namespace audiorgui {

struct GpuInfo {
    std::wstring  name;
    std::uint64_t memoryBytes = 0;          /* its own (dedicated) memory; a card that shares the PC's has little */
};

struct HardwareProfile {
    std::vector<GpuInfo> gpus;              /* hardware adapters only: not Windows' software renderer */
    bool                 vulkanDriver = false;      /* the PC has a Vulkan driver (the graphics driver installs it) */
};

enum class GpuUse {
    Usable,                                 /* a card, a driver and a helper that can use it          */
    NoCard,                                 /* no graphics hardware found                             */
    NoDriver,                               /* a card, but no Vulkan driver for it                    */
    NoBackend,                              /* a card, but the helper was built without GPU support   */
};

struct GpuVerdict {
    GpuUse        use = GpuUse::NoCard;
    std::wstring  card;                     /* the card with the most memory of its own; empty if none */
    std::uint64_t cardBytes = 0;
};

/* Whether the helper can work on the graphics card of `hw`, given whether it has the backend for it. */
GpuVerdict JudgeGpu(const HardwareProfile &hw, bool backendPresent);

/* One sentence for the user: "NVIDIA GeForce RTX 5080 (16.0 GB) will transcribe." or why the processor does. */
std::wstring DescribeGpu(const GpuVerdict &gpu);

struct SetupPlan {
    GpuVerdict                    gpu;
    std::vector<const AsrModel *> download;     /* the files to fetch, in order; empty: nothing is missing */
    std::uint64_t                 bytes = 0;    /* their total size */
};

/* What to do about speech models.  A PC that has any model installed is left as it is.  One that has
 * none is offered every model of `models` that suits its hardware (see AsrModel::gpuMemoryBytes and
 * gpuMemoryBelowBytes), and
 * the voice-activity model `vad` besides, unless it is installed already.  (`models` is the table; the
 * tests give it a small one.) */
SetupPlan PlanSetup(const HardwareProfile &hw, bool backendPresent, const std::vector<const AsrModel *> &installed,
                    bool vadInstalled, const AsrModel *models = kAsrModels, std::size_t modelCount = kAsrModelCount,
                    const AsrModel *vad = &kVadModel);

}  // namespace audiorgui
