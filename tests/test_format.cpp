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
