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
#include <cwchar>

#include "../src/engine/stop_signal.h"
#include "fake_asr_helper.h"
#include "test_support.h"

namespace testing {

namespace {
bool        g_currentFailed = false;
bool        g_currentSkipped = false;
std::string g_skipReason;
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

void ReportSkip(const std::string &reason)
{
    g_currentSkipped = true;
    g_skipReason = reason;
}

}  // namespace testing

int main(int argc, char **argv)
{
    using namespace testing;
    if (argc > 2 && std::strcmp(argv[1], kFakeAsrHelperFlag) == 0)      /* started by a test, to play the helper */
        return RunFakeAsrHelper(argv[2]);
    const char *filter = argc > 1 ? argv[1] : nullptr;

    if (filter && std::strcmp(filter, "--list") == 0) {
        for (const Case &c : Registry())
            std::printf("%s\n", c.name);
        return 0;
    }

    /* The recorder's stop event has a machine-wide name.  The tests use one of their own, so that they can
     * neither see nor stop a recording (of anyone's) that is really running, nor one another's run. */
    static wchar_t stopEvent[64];
    std::swprintf(stopEvent, sizeof(stopEvent) / sizeof(stopEvent[0]), L"Local\\AudiorStopEvent.test.%lu",
                  GetCurrentProcessId());
    audiorgui::UseStopEventName(stopEvent);

    /* Some tests use the audio device APIs, which need COM. */
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    int run = 0, failed = 0, skipped = 0;
    for (const Case &c : Registry()) {
        if (filter && !std::strstr(c.name, filter))
            continue;
        g_currentFailed = false;
        g_currentSkipped = false;
        c.fn();
        ++run;
        if (g_currentFailed) {
            ++failed;
            std::printf("  FAIL  %s\n", c.name);
        } else if (g_currentSkipped) {
            ++skipped;
            std::printf("  skip  %s (%s)\n", c.name, g_skipReason.c_str());
        }
    }
    std::printf("%d tests, %d passed, %d failed", run, run - failed - skipped, failed);
    if (skipped > 0)
        std::printf(", %d skipped", skipped);
    std::printf("\n");
    if (SUCCEEDED(com))
        CoUninitialize();
    return failed;
}
