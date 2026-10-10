#include "../src/core/peak_history.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

PeakBin Bin(float low, float high)
{
    PeakBin b;
    b.low = low;
    b.high = high;
    return b;
}

}  // namespace

TEST(PeakHistory_StartsEmpty)
{
    PeakHistory h(10);
    CHECK_EQ(h.Total(), std::uint64_t(0));
    CHECK_EQ(h.Oldest(), std::uint64_t(0));
    const PeakBin p = h.Span(0, 5);
    CHECK_NEAR(p.low, 0.0, 0.0);
    CHECK_NEAR(p.high, 0.0, 0.0);
}

TEST(PeakHistory_SpanIsTheExtremeOfItsBins)
{
    PeakHistory h(10);
    const PeakBin bins[] = {Bin(-0.1f, 0.2f), Bin(-0.5f, 0.1f), Bin(-0.2f, 0.7f), Bin(-0.3f, 0.3f)};
    h.Append(bins, 4);
    CHECK_EQ(h.Total(), std::uint64_t(4));

    PeakBin p = h.Span(0, 4);
    CHECK_NEAR(p.low, -0.5, 1e-6);
    CHECK_NEAR(p.high, 0.7, 1e-6);

    p = h.Span(2, 4);                       /* only the last two bins */
    CHECK_NEAR(p.low, -0.3, 1e-6);
    CHECK_NEAR(p.high, 0.7, 1e-6);

    p = h.Span(1, 2);                       /* a single bin */
    CHECK_NEAR(p.low, -0.5, 1e-6);
    CHECK_NEAR(p.high, 0.1, 1e-6);
}

TEST(PeakHistory_AppendInSeveralCallsIsTheSameAsOne)
{
    PeakHistory a(10), b(10);
    const PeakBin bins[] = {Bin(-0.1f, 0.2f), Bin(-0.4f, 0.1f), Bin(-0.2f, 0.9f)};
    a.Append(bins, 3);
    b.Append(bins, 1);
    b.Append(bins + 1, 2);
    CHECK_EQ(a.Total(), b.Total());
    CHECK_NEAR(a.Span(0, 3).low, b.Span(0, 3).low, 0.0);
    CHECK_NEAR(a.Span(0, 3).high, b.Span(0, 3).high, 0.0);
}

TEST(PeakHistory_KeepsOnlyTheNewestBins)
{
    PeakHistory h(4);
    for (int i = 1; i <= 6; ++i) {          /* bins 0..5 with high = 0.1 * (bin + 1) */
        const PeakBin b = Bin(0.0f, 0.1f * static_cast<float>(i));
        h.Append(&b, 1);
    }
    CHECK_EQ(h.Total(), std::uint64_t(6));
    CHECK_EQ(h.Oldest(), std::uint64_t(2));

    CHECK_NEAR(h.Span(2, 3).high, 0.3, 1e-6);       /* oldest kept: bin 2 */
    CHECK_NEAR(h.Span(5, 6).high, 0.6, 1e-6);       /* newest */
    CHECK_NEAR(h.Span(0, 2).high, 0.0, 0.0);        /* dropped bins read as silence */
}

TEST(PeakHistory_OutOfRangeSpansAreSilence)
{
    PeakHistory h(10);
    const PeakBin b = Bin(-0.4f, 0.4f);
    h.Append(&b, 1);
    CHECK_NEAR(h.Span(5, 9).high, 0.0, 0.0);        /* not received yet */
    CHECK_NEAR(h.Span(3, 3).high, 0.0, 0.0);        /* empty */
    CHECK_NEAR(h.Span(4, 2).high, 0.0, 0.0);        /* backwards */
    CHECK_NEAR(h.Span(0, 100).high, 0.4, 1e-6);    /* clipped to what exists */
}

TEST(PeakHistory_ClearForgetsEverything)
{
    PeakHistory h(4);
    const PeakBin b = Bin(-1.0f, 1.0f);
    h.Append(&b, 1);
    h.Clear();
    CHECK_EQ(h.Total(), std::uint64_t(0));
    CHECK_NEAR(h.Span(0, 1).high, 0.0, 0.0);
}

TEST(PeakHistory_ZeroCapacityStillWorks)
{
    PeakHistory h(0);
    const PeakBin b = Bin(-0.2f, 0.2f);
    h.Append(&b, 1);
    CHECK_EQ(h.Total(), std::uint64_t(1));
}
