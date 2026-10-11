#include "format.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <limits>

namespace audiorgui {

std::wstring FormatDuration(double seconds)
{
    if (!(seconds > 0.0))       /* also catches NaN */
        seconds = 0.0;
    const std::uint64_t total = static_cast<std::uint64_t>(std::min(seconds, 1e12));
    wchar_t buf[40];
    std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%02llu:%02llu:%02llu",
                  static_cast<unsigned long long>(total / 3600),
                  static_cast<unsigned long long>(total / 60 % 60),
                  static_cast<unsigned long long>(total % 60));
    return buf;
}

std::wstring FormatClock(double seconds)
{
    if (!(seconds > 0.0))
        seconds = 0.0;
    const unsigned long long total = static_cast<unsigned long long>(std::min(seconds, 1e12));
    wchar_t buf[40];
    if (total >= 3600)
        std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%llu:%02llu:%02llu", total / 3600, total / 60 % 60,
                      total % 60);
    else
        std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%llu:%02llu", total / 60, total % 60);
    return buf;
}

std::wstring FormatLag(float seconds)
{
    if (!(seconds > 0.0f))
        seconds = 0.0f;
    const long tenths = std::lround(std::min(seconds, 99999.0f) * 10.0f);
    wchar_t buf[40];
    std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%ld.%ld s", tenths / 10, tenths % 10);
    return buf;
}

std::wstring FormatBytes(std::uint64_t bytes)
{
    static const wchar_t *const kUnits[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    if (bytes < 1024) {
        wchar_t buf[40];
        std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%llu B",
                      static_cast<unsigned long long>(bytes));
        return buf;
    }
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1023.95 && unit < 4) {     /* so "%.1f" never prints 1024.0 */
        value /= 1024.0;
        ++unit;
    }
    wchar_t buf[40];
    std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%.1f %ls", value, kUnits[unit]);
    return buf;
}

float LinearToDb(float linear)
{
    if (!(linear > 0.0f))
        return -std::numeric_limits<float>::infinity();
    return 20.0f * std::log10(linear);
}

std::wstring FormatDb(float db)
{
    if (!std::isfinite(db))
        return L"-∞ dB";
    wchar_t buf[40];    /* rounded as an integer, so -0.3 reads "0 dB", not "-0 dB" */
    std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%ld dB", std::lround(db));
    return buf;
}

bool ParseWholeNumber(const std::wstring &text, int *value)
{
    if (text.empty() || text.size() > 9)
        return false;
    int v = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9')
            return false;
        v = v * 10 + (c - L'0');
    }
    *value = v;
    return true;
}

}  // namespace audiorgui
