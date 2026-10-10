/*
 * test_support.h - A very small unit-test harness (no external dependencies).
 *
 *     TEST(Name) { CHECK(x); CHECK_EQ(a, b); CHECK_NEAR(a, b, eps); }
 *
 * Tests register themselves; test_main.cpp runs them.
 */
#pragma once

#include <cmath>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace testing {

struct Case {
    const char *name;
    void      (*fn)();
};

std::vector<Case> &Registry();
void ReportFailure(const char *file, int line, const std::string &message);

struct Registrar {
    Registrar(const char *name, void (*fn)()) { Registry().push_back({name, fn}); }
};

inline std::string Describe(const std::wstring &w)
{
    std::string s = "L\"";
    for (wchar_t c : w)
        s += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
    return s + "\"";
}

inline std::string Describe(const wchar_t *w) { return Describe(std::wstring(w)); }

template <typename T>
std::string Describe(const T &v)
{
    if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<long long>(v));
    } else if constexpr (std::is_same_v<T, bool>) {
        return v ? "true" : "false";
    } else if constexpr (std::is_arithmetic_v<T>) {
        std::ostringstream out;
        out << v;
        return out.str();
    } else {
        return "<value>";
    }
}

}  // namespace testing

#define TEST(name)                                                    \
    static void name();                                               \
    static ::testing::Registrar registrar_##name(#name, name);        \
    static void name()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond))                                                                 \
            ::testing::ReportFailure(__FILE__, __LINE__, "CHECK(" #cond ") failed");  \
    } while (0)

#define CHECK_EQ(a, b)                                                               \
    do {                                                                             \
        const auto &va_ = (a);                                                       \
        const auto &vb_ = (b);                                                       \
        if (!(va_ == vb_))                                                           \
            ::testing::ReportFailure(__FILE__, __LINE__,                             \
                std::string("CHECK_EQ(" #a ", " #b ") failed: ") +                   \
                ::testing::Describe(va_) + " vs " + ::testing::Describe(vb_));       \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
    do {                                                                             \
        const double va_ = static_cast<double>(a);                                   \
        const double vb_ = static_cast<double>(b);                                   \
        if (!(std::fabs(va_ - vb_) <= (eps)))                                        \
            ::testing::ReportFailure(__FILE__, __LINE__,                             \
                std::string("CHECK_NEAR(" #a ", " #b ") failed: ") +                 \
                ::testing::Describe(va_) + " vs " + ::testing::Describe(vb_));       \
    } while (0)
