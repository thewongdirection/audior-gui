/*
 * test_main.cpp - Runs every registered test.
 *
 *     run_tests.exe              run everything
 *     run_tests.exe <fragment>   run the tests whose name contains <fragment>
 *     run_tests.exe --list       list the tests
 *
 * The exit code is the number of failed tests.
 */
#include <windows.h>

#include <objbase.h>

#include <cstdio>
#include <cstring>

#include "test_support.h"

namespace testing {

namespace {
bool g_currentFailed = false;
}

std::vector<Case> &Registry()
{
    static std::vector<Case> cases;
    return cases;
}

void ReportFailure(const char *file, int line, const std::string &message)
{
    g_currentFailed = true;
    std::printf("    %s(%d): %s\n", file, line, message.c_str());
}

}  // namespace testing

int main(int argc, char **argv)
{
    using namespace testing;
    const char *filter = argc > 1 ? argv[1] : nullptr;

    if (filter && std::strcmp(filter, "--list") == 0) {
        for (const Case &c : Registry())
            std::printf("%s\n", c.name);
        return 0;
    }

    /* Some tests use the audio device APIs, which need COM. */
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    int run = 0, failed = 0;
    for (const Case &c : Registry()) {
        if (filter && !std::strstr(c.name, filter))
            continue;
        g_currentFailed = false;
        c.fn();
        ++run;
        if (g_currentFailed) {
            ++failed;
            std::printf("  FAIL  %s\n", c.name);
        }
    }
    std::printf("%d tests, %d passed, %d failed\n", run, run - failed, failed);
    if (SUCCEEDED(com))
        CoUninitialize();
    return failed;
}
