#include "../src/core/pane_split.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

constexpr float kGap = 16.0f, kBar = 48.0f;

}  // namespace

TEST(PaneSplit_TwoPanesShareTheRoomEqually)
{
    const PaneSplit s = SplitPanes(544.0f, kGap, MiddleCard::None, kBar);
    CHECK_NEAR(s.pane, 264.0, 1e-4);                    /* (544 - 16) / 2 */
    CHECK_NEAR(s.middle, 0.0, 1e-4);
    CHECK_NEAR(SplitPanes(0.0f, kGap, MiddleCard::None, kBar).pane, 0.0, 1e-4);   /* never negative */
}

TEST(PaneSplit_ABarTakesItsHeightAndThePanesTheRest)
{
    const PaneSplit s = SplitPanes(544.0f, kGap, MiddleCard::Bar, kBar);
    CHECK_NEAR(s.middle, kBar, 1e-4);
    CHECK_NEAR(s.pane, (544.0 - 2 * kGap - kBar) * 0.5, 1e-4);
    CHECK_NEAR(2 * s.pane + s.middle + 2 * kGap, 544.0, 1e-3);
}

TEST(PaneSplit_AnOpenCardTakesWhatThePanesCanSpare)
{
    const PaneSplit s = SplitPanes(544.0f, kGap, MiddleCard::Open, kBar);
    CHECK_NEAR(s.pane, 544.0 * 0.3, 1e-3);              /* 163.2: between 150 and 200 */
    CHECK(s.middle > s.pane);                           /* the text has the most room */
    CHECK_NEAR(2 * s.pane + s.middle + 2 * kGap, 544.0, 1e-3);
}

TEST(PaneSplit_APaneBesideAnOpenCardKeepsItsControlsAndAWaveform)
{
    const PaneSplit small = SplitPanes(446.0f, kGap, MiddleCard::Open, kBar);    /* the narrowest window with a notification */
    CHECK_NEAR(small.pane, 150.0, 1e-3);                /* 30 % of 446 is 134: not below the least */
    CHECK_NEAR(2 * small.pane + small.middle + 2 * kGap, 446.0, 1e-3);
    CHECK(small.middle >= kBar);

    const PaneSplit big = SplitPanes(2000.0f, kGap, MiddleCard::Open, kBar);
    CHECK_NEAR(big.pane, 200.0, 1e-3);                  /* a tall window gives the text the rest, not the waveforms */
    CHECK_NEAR(2 * big.pane + big.middle + 2 * kGap, 2000.0, 1e-3);
}

TEST(PaneSplit_AnOpenCardIsNeverShorterThanTheBar)
{
    for (float room = 0.0f; room <= 700.0f; room += 7.0f) {
        const PaneSplit s = SplitPanes(room, kGap, MiddleCard::Open, kBar);
        CHECK(s.pane >= 0.0f);
        if (room >= 2 * kGap + kBar) {                  /* when there is room for the card at all */
            CHECK(s.middle >= kBar - 1e-3f);
            CHECK_NEAR(2 * s.pane + s.middle + 2 * kGap, room, 1e-3);          /* nothing is lost or made up */
        } else {
            CHECK_NEAR(s.pane, 0.0, 1e-4);              /* a window too small to show anything is not drawn into negative sizes */
        }
    }
}

TEST(PaneSplit_TheBarAndTheOpenCardAgreeWhenThereIsNoRoomToSpare)
{
    const PaneSplit bar = SplitPanes(300.0f, kGap, MiddleCard::Bar, kBar);
    const PaneSplit open = SplitPanes(300.0f, kGap, MiddleCard::Open, kBar);
    CHECK_NEAR(open.pane, bar.pane, 1e-4);              /* the least a pane may be is no more than the room has */
    CHECK_NEAR(open.middle, bar.middle, 1e-4);
}
