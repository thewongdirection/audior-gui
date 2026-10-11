#include <limits>

#include "../src/core/format.h"
#include "test_support.h"

using namespace audiorgui;

TEST(FormatDuration_Basics)
{
    CHECK_EQ(FormatDuration(0), std::wstring(L"00:00:00"));
    CHECK_EQ(FormatDuration(0.99), std::wstring(L"00:00:00"));
    CHECK_EQ(FormatDuration(1), std::wstring(L"00:00:01"));
    CHECK_EQ(FormatDuration(59.9), std::wstring(L"00:00:59"));
    CHECK_EQ(FormatDuration(61), std::wstring(L"00:01:01"));
    CHECK_EQ(FormatDuration(3599), std::wstring(L"00:59:59"));
    CHECK_EQ(FormatDuration(3600), std::wstring(L"01:00:00"));
    CHECK_EQ(FormatDuration(3 * 3600 + 25 * 60 + 9), std::wstring(L"03:25:09"));
}

TEST(FormatDuration_HoursGrowBeyondTwoDigits)
{
    CHECK_EQ(FormatDuration(100.0 * 3600), std::wstring(L"100:00:00"));
}

TEST(FormatClock_Basics)
{
    CHECK_EQ(FormatClock(0), std::wstring(L"0:00"));
    CHECK_EQ(FormatClock(7.9), std::wstring(L"0:07"));
    CHECK_EQ(FormatClock(754), std::wstring(L"12:34"));
    CHECK_EQ(FormatClock(3599), std::wstring(L"59:59"));
    CHECK_EQ(FormatClock(3600), std::wstring(L"1:00:00"));
    CHECK_EQ(FormatClock(3723), std::wstring(L"1:02:03"));
}

TEST(FormatClock_ClampsNonsense)
{
    CHECK_EQ(FormatClock(-3), std::wstring(L"0:00"));
    CHECK_EQ(FormatClock(std::numeric_limits<double>::quiet_NaN()), std::wstring(L"0:00"));
    CHECK(!FormatClock(1e300).empty());
}

TEST(FormatLag_RoundsToATenth)
{
    CHECK_EQ(FormatLag(0.0f), std::wstring(L"0.0 s"));
    CHECK_EQ(FormatLag(0.4f), std::wstring(L"0.4 s"));
    CHECK_EQ(FormatLag(1.26f), std::wstring(L"1.3 s"));
    CHECK_EQ(FormatLag(12.0f), std::wstring(L"12.0 s"));
    CHECK_EQ(FormatLag(-1.0f), std::wstring(L"0.0 s"));
    CHECK_EQ(FormatLag(std::numeric_limits<float>::infinity()).empty(), false);
}

TEST(FormatDuration_ClampsNonsense)
{
    CHECK_EQ(FormatDuration(-5), std::wstring(L"00:00:00"));
    CHECK_EQ(FormatDuration(std::numeric_limits<double>::quiet_NaN()), std::wstring(L"00:00:00"));
    CHECK(!FormatDuration(1e300).empty());                  /* no overflow */
}

TEST(FormatBytes_Units)
{
    CHECK_EQ(FormatBytes(0), std::wstring(L"0 B"));
    CHECK_EQ(FormatBytes(1023), std::wstring(L"1023 B"));
    CHECK_EQ(FormatBytes(1024), std::wstring(L"1.0 KB"));
    CHECK_EQ(FormatBytes(1536), std::wstring(L"1.5 KB"));
    CHECK_EQ(FormatBytes(5u * 1024 * 1024), std::wstring(L"5.0 MB"));
    CHECK_EQ(FormatBytes(std::uint64_t(3) * 1024 * 1024 * 1024), std::wstring(L"3.0 GB"));
    CHECK_EQ(FormatBytes(std::uint64_t(2) * 1024 * 1024 * 1024 * 1024), std::wstring(L"2.0 TB"));
}

TEST(FormatBytes_HugeValuesStayInTheLargestUnit)
{
    const std::wstring text = FormatBytes(std::numeric_limits<std::uint64_t>::max());
    CHECK(text.size() > 2 && text.substr(text.size() - 2) == L"TB");
}

TEST(LinearToDb_KnownPoints)
{
    CHECK_NEAR(LinearToDb(1.0f), 0.0, 1e-6);
    CHECK_NEAR(LinearToDb(0.5f), -6.0206, 1e-3);
    CHECK_NEAR(LinearToDb(0.1f), -20.0, 1e-4);
    CHECK(std::isinf(LinearToDb(0.0f)) && LinearToDb(0.0f) < 0);
    CHECK(std::isinf(LinearToDb(-0.5f)));
}

TEST(FormatDb_RoundsAndShowsSilence)
{
    CHECK_EQ(FormatDb(-12.4f), std::wstring(L"-12 dB"));
    CHECK_EQ(FormatDb(-12.6f), std::wstring(L"-13 dB"));
    CHECK_EQ(FormatDb(0.0f), std::wstring(L"0 dB"));
    CHECK_EQ(FormatDb(-0.3f), std::wstring(L"0 dB"));          /* never "-0 dB" */
    CHECK_EQ(FormatDb(-std::numeric_limits<float>::infinity()), std::wstring(L"-∞ dB"));
}

TEST(FormatBytes_NeverShowsTheNextUnitAs1024)
{
    CHECK_EQ(FormatBytes(1048575), std::wstring(L"1.0 MB"));        /* not "1024.0 KB" */
    CHECK_EQ(FormatBytes(1048576), std::wstring(L"1.0 MB"));
    CHECK_EQ(FormatBytes(1023 * 1024), std::wstring(L"1023.0 KB"));
}

TEST(FormatTranscriptLine_HasTheClockThePanesShow)
{
    CHECK_EQ(FormatTranscriptLine(0.0, L"Hello."), std::wstring(L"[0:00] Hello."));
    CHECK_EQ(FormatTranscriptLine(6.9, L"The weather today."), std::wstring(L"[0:06] The weather today."));
    CHECK_EQ(FormatTranscriptLine(754.0, L"Twelve minutes in."), std::wstring(L"[12:34] Twelve minutes in."));
    CHECK_EQ(FormatTranscriptLine(3725.0, L"An hour in."), std::wstring(L"[1:02:05] An hour in."));
    CHECK_EQ(FormatTranscriptLine(-3.0, L"Before the start."), std::wstring(L"[0:00] Before the start."));
}

TEST(FormatTranscriptLine_IsOneLineAndNeverEmpty)
{
    CHECK_EQ(FormatTranscriptLine(1.0, L"one\r\ntwo\tthree"), std::wstring(L"[0:01] one  two three"));
    CHECK_EQ(FormatTranscriptLine(1.0, L"…"), std::wstring(L"[0:01] …"));                /* a phrase that was not decoded */
    CHECK(FormatTranscriptLine(1.0, L"").empty());                                          /* a phrase taken back */
    CHECK(FormatTranscriptLine(1.0, L"  \t\r\n ").empty());
}
