#include <cmath>

#include "../src/core/peak_meter.h"
#include "test_support.h"

using namespace audiorgui;


TEST(PeakMeter_JumpsUpAndFallsSteadily)
{
    PeakMeter m;
    CHECK(std::isinf(m.LevelDb()));         /* silent at first */

    m.Feed(0.5f);
    CHECK_NEAR(m.LevelDb(), -6.02, 0.01);

    m.Advance(0.5);                         /* 24 dB/s for half a second */
    CHECK_NEAR(m.LevelDb(), -6.02 - 12.0, 0.01);

    m.Feed(0.1f);                           /* a quieter sound does not pull it down */
    CHECK_NEAR(m.LevelDb(), -6.02 - 12.0, 0.01);
    m.Feed(0.9f);                           /* a louder one raises it */
    CHECK_NEAR(m.LevelDb(), -0.915, 0.01);
}

TEST(PeakMeter_FallsToSilence)
{
    PeakMeter m;
    m.Feed(1.0f);
    m.Advance(10.0);
    CHECK(std::isinf(m.LevelDb()));
}

TEST(PeakMeter_ClipFlagHoldsThenReleases)
{
    PeakMeter m;
    m.Feed(0.95f);
    CHECK(!m.Clipping());
    m.Feed(1.0f);
    CHECK(m.Clipping());
    m.Advance(1.0);
    CHECK(m.Clipping());                    /* held for 1.5 s */
    m.Advance(0.6);
    CHECK(!m.Clipping());
}

TEST(PeakMeter_ResetAndNegativeTimeAreHarmless)
{
    PeakMeter m;
    m.Feed(1.0f);
    m.Advance(-1.0);
    CHECK_NEAR(m.LevelDb(), 0.0, 1e-6);     /* time never runs backwards */
    m.Reset();
    CHECK(std::isinf(m.LevelDb()));
    CHECK(!m.Clipping());
}

TEST(PeakMeter_IsActiveUntilItHasFallenSilent)
{
    PeakMeter m;
    CHECK(!m.Active());
    m.Feed(1.0f);                           /* loud, and clipping */
    CHECK(m.Active());
    m.Advance(2.0);                         /* the clip hold has ended; the level still falls */
    CHECK(!m.Clipping());
    CHECK(m.Active());
    m.Advance(10.0);
    CHECK(!m.Active());
}
