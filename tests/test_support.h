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
void ReportSkip(const std::string &reason);

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

/* The comparisons take their operands as function arguments, so temporaries
 * (including references into them, like `MakeVector()[0]`) live until the
 * comparison is done. */
template <typename A, typename B>
void CheckEq(const char *file, int line, const char *expression, const A &a, const B &b)
{
    if (!(a == b))
        ReportFailure(file, line,
                      std::string("CHECK_EQ(") + expression + ") failed: " + Describe(a) + " vs " + Describe(b));
}

inline void CheckNear(const char *file, int line, const char *expression, double a, double b, double epsilon)
{
    if (!(std::fabs(a - b) <= epsilon))
        ReportFailure(file, line,
                      std::string("CHECK_NEAR(") + expression + ") failed: " + Describe(a) + " vs " + Describe(b));
}

}  // namespace testing

#define TEST(name)                                                    \
    static void name();                                               \
    static ::testing::Registrar registrar_##name(#name, name);        \
    static void name()

/* For a test that needs something this machine does not have (a model, a helper): it says so and is
 * counted as skipped, not as passed. */
#define SKIP(reason)                        \
    do {                                    \
        ::testing::ReportSkip(reason);      \
        return;                             \
    } while (0)

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond))                                                                 \
            ::testing::ReportFailure(__FILE__, __LINE__, "CHECK(" #cond ") failed");  \
    } while (0)

#define CHECK_EQ(a, b) ::testing::CheckEq(__FILE__, __LINE__, #a ", " #b, (a), (b))

#define CHECK_NEAR(a, b, epsilon) \
    ::testing::CheckNear(__FILE__, __LINE__, #a ", " #b, static_cast<double>(a), static_cast<double>(b), (epsilon))
