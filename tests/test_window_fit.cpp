#include "../src/core/window_fit.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

const PixelRect kWork = {0, 0, 2560, 1368};             /* a 2560 x 1440 monitor without a 72 px taskbar */

}  // namespace

TEST(WindowFit_AWindowThatFitsStaysWhereItIs)
{
    const PixelRect w = {76, 76, 1778, 1233};
    CHECK(FitIntoWorkArea(w, kWork) == w);
    const PixelRect edge = {858, 211, 2560, 1368};      /* touching the edges is inside */
    CHECK(FitIntoWorkArea(edge, kWork) == edge);
}

TEST(WindowFit_AWindowUnderTheTaskbarMovesUpByExactlyThatMuch)
{
    const PixelRect cascaded = {304, 283, 2006, 1440};  /* what Windows chose on the PC where this was seen */
    const PixelRect fitted = FitIntoWorkArea(cascaded, kWork);
    CHECK_EQ(fitted.bottom, 1368);
    CHECK_EQ(fitted.top, 211);
    CHECK_EQ(fitted.left, 304);                         /* the other direction is not touched */
    CHECK_EQ(fitted.Width(), 1702);                     /* and the size stays */
    CHECK_EQ(fitted.Height(), 1157);
}

TEST(WindowFit_AWindowPastAnyEdgeMovesBackIn)
{
    const PixelRect right = FitIntoWorkArea({2000, 100, 3700, 1200}, kWork);
    CHECK_EQ(right.right, 2560);
    CHECK_EQ(right.Width(), 1700);
    const PixelRect left = FitIntoWorkArea({-300, 100, 1400, 1200}, kWork);
    CHECK_EQ(left.left, 0);
    CHECK_EQ(left.Width(), 1700);
    const PixelRect top = FitIntoWorkArea({100, -50, 1000, 800}, kWork);
    CHECK_EQ(top.top, 0);
    CHECK_EQ(top.Height(), 850);
}

TEST(WindowFit_AWindowBiggerThanTheWorkAreaIsCutDownToIt)
{
    const PixelRect small = {0, 0, 1280, 672};          /* a 1280 x 720 monitor with a taskbar */
    const PixelRect fitted = FitIntoWorkArea({40, 30, 1742, 1187}, small);
    CHECK(fitted == small);
    const PixelRect tall = FitIntoWorkArea({200, 30, 1000, 1187}, small);      /* only too tall */
    CHECK_EQ(tall.top, 0);
    CHECK_EQ(tall.bottom, 672);
    CHECK_EQ(tall.left, 200);
    CHECK_EQ(tall.Width(), 800);
}

TEST(WindowFit_AWorkAreaThatDoesNotStartAtZeroIsRespected)
{
    const PixelRect second = {2560, 0, 4480, 1040};     /* a second monitor to the right */
    const PixelRect fitted = FitIntoWorkArea({4000, 700, 5702, 1857}, second);
    CHECK_EQ(fitted.right, 4480);
    CHECK_EQ(fitted.bottom, 1040);
    CHECK_EQ(fitted.Width(), 1702);                     /* moved in, not cut: the area is 1920 wide */
    CHECK_EQ(fitted.top, 0);
    CHECK_EQ(fitted.Height(), 1040);                    /* 1157 does not fit in 1040: cut to the area */
}
