/* The hardware scan runs on whatever machine the tests run on, which may have no graphics card at all
 * (a build server), so it is checked for sense, not for a particular answer; the decisions made from
 * what it finds are tested with made-up hardware (test_setup_plan.cpp). */
#include "../src/core/setup_plan.h"
#include "../src/engine/hardware_scan.h"
#include "test_support.h"

using namespace audiorgui;

TEST(Hardware_TheScanReportsOnlyRealAdaptersWithNames)
{
    const HardwareProfile hw = ScanHardware();
    for (const GpuInfo &gpu : hw.gpus) {
        CHECK(!gpu.name.empty());
        CHECK(gpu.name != L"Microsoft Basic Render Driver");           /* Windows' software renderer is not a card */
        CHECK(gpu.name.back() != L' ');
    }
}

TEST(Hardware_TheScanCanBeRepeated)
{
    const HardwareProfile first = ScanHardware(), second = ScanHardware();
    CHECK_EQ(first.gpus.size(), second.gpus.size());
    CHECK_EQ(first.vulkanDriver, second.vulkanDriver);
}

TEST(Hardware_WhateverIsFoundCanBeJudged)
{
    const HardwareProfile hw = ScanHardware();
    const GpuVerdict v = JudgeGpu(hw, true);
    CHECK(v.use != GpuUse::NoBackend);                  /* the helper was said to have its backend */
    CHECK_EQ(hw.gpus.empty(), v.use == GpuUse::NoCard); /* a card found is a card judged, and none found is none */
    CHECK_EQ(v.use == GpuUse::NoCard, v.card.empty());
    CHECK(!DescribeGpu(v).empty());
}
