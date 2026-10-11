/*
 * app_process.h - Starts and watches the real Audior-GUI.exe for the hardware tests.
 *
 * A window can be started "in the background": visible, fully usable, but at the bottom of the
 * window stack and never activated, so it neither covers what the person at the PC is doing
 * nor takes the keyboard focus.  (The program passes its launch show-command straight to
 * ShowWindow, so it comes up minimized and not activated, which puts it in the tray, hidden;
 * it is then restored without activation and sent to the bottom.)
 */
#pragma once

#include <windows.h>

#include <string>
#include <vector>

class AppProcess {
public:
    AppProcess() = default;
    ~AppProcess();
    AppProcess(const AppProcess &) = delete;
    AppProcess &operator=(const AppProcess &) = delete;

    /* Starts `exe` with `args` (already quoted where needed) and waits for its window, or for
     * the program to end without one.  False if it could not be started at all. */
    bool Start(const std::wstring &exe, const std::wstring &args, bool background);

    DWORD Id() const { return pid_; }
    bool  HasWindow() const { return window_ != nullptr; }
    bool  Exited() const { return process_ && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0; }

    /* Asks the window to close (as its close button does) and waits; kills it if it will not.
     * Returns the exit code, or -1 if it had to be killed. */
    int CloseWindow(DWORD timeoutMs = 10000);
    /* Waits for the program to end by itself; kills it and returns -1 after the timeout. */
    int WaitForExit(DWORD timeoutMs);

    struct Usage {
        double privateMb = 0.0;
        DWORD  handles = 0;
        DWORD  threads = 0;
        double cpuSeconds = 0.0;
    };
    Usage Sample() const;

private:
    HANDLE process_ = nullptr;
    DWORD  pid_ = 0;
    HWND   window_ = nullptr;
};

struct CliResult {
    int         exitCode = -1;
    std::string output;             /* standard output and standard error together */
};

/* Runs `exe args` to completion (the command-line modes: --list-devices, --stop, bad options). */
CliResult RunCli(const std::wstring &exe, const std::wstring &args, DWORD timeoutMs = 20000);

/* The process that owns the window the keyboard is going to (0 if none). */
DWORD ForegroundProcessId();

/* The ids of the running processes whose program file is called `exeName` (any folder). */
std::vector<DWORD> FindProcesses(const wchar_t *exeName);
