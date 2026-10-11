#include "app_process.h"

#include <tlhelp32.h>
#include <psapi.h>

#include <vector>

namespace {

constexpr DWORD kWindowWaitMs = 10000;

struct WindowSearch {
    DWORD pid;
    HWND  found;
};

BOOL CALLBACK FindVisibleWindow(HWND window, LPARAM param)
{
    WindowSearch *search = reinterpret_cast<WindowSearch *>(param);
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != search->pid || !IsWindowVisible(window))
        return TRUE;
    search->found = window;
    return FALSE;
}

double Seconds(const FILETIME &t)
{
    ULARGE_INTEGER v;
    v.LowPart = t.dwLowDateTime;
    v.HighPart = t.dwHighDateTime;
    return static_cast<double>(v.QuadPart) / 1e7;
}

/* Starts a process; with `pipeOut` its output (stdout and stderr) goes to that handle. */
bool Create(const std::wstring &exe, const std::wstring &args, WORD show, HANDLE pipeOut, PROCESS_INFORMATION *pi)
{
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = show;
    if (pipeOut) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = pipeOut;
        si.hStdError = pipeOut;
        si.hStdInput = nullptr;                         /* the parent's may be missing or not inheritable */
    }
    std::wstring command = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
    return CreateProcessW(nullptr, command.data(), nullptr, nullptr, pipeOut != nullptr, 0, nullptr, nullptr, &si, pi) != FALSE;
}

}  // namespace

AppProcess::~AppProcess()
{
    if (process_) {
        if (!Exited())
            TerminateProcess(process_, 1);
        CloseHandle(process_);
    }
}

bool AppProcess::Start(const std::wstring &exe, const std::wstring &args, bool background)
{
    PROCESS_INFORMATION pi = {};
    if (!Create(exe, args, background ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL, nullptr, &pi))
        return false;
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    pid_ = pi.dwProcessId;

    for (DWORD waited = 0; waited < kWindowWaitMs && !window_ && !Exited(); waited += 10) {
        WindowSearch search = {pid_, nullptr};
        EnumWindows(FindVisibleWindow, reinterpret_cast<LPARAM>(&search));
        window_ = search.found;
        if (!window_)
            Sleep(10);
    }
    if (window_ && background) {
        if (IsIconic(window_))
            ShowWindow(window_, SW_SHOWNOACTIVATE);
        SetWindowPos(window_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return true;
}

int AppProcess::WaitForExit(DWORD timeoutMs)
{
    if (WaitForSingleObject(process_, timeoutMs) != WAIT_OBJECT_0) {
        TerminateProcess(process_, 1);
        WaitForSingleObject(process_, 5000);
        return -1;
    }
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    return static_cast<int>(code);
}

int AppProcess::CloseWindow(DWORD timeoutMs)
{
    if (window_ && !Exited())
        PostMessageW(window_, WM_CLOSE, 0, 0);
    return WaitForExit(timeoutMs);
}

AppProcess::Usage AppProcess::Sample() const
{
    Usage usage;
    PROCESS_MEMORY_COUNTERS_EX memory = {};
    memory.cb = sizeof memory;
    if (GetProcessMemoryInfo(process_, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof memory))
        usage.privateMb = static_cast<double>(memory.PrivateUsage) / (1024.0 * 1024.0);
    GetProcessHandleCount(process_, &usage.handles);
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(process_, &created, &exited, &kernel, &user))
        usage.cpuSeconds = Seconds(kernel) + Seconds(user);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry = {};
        entry.dwSize = sizeof entry;
        for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID == pid_)
                ++usage.threads;
        }
        CloseHandle(snapshot);
    }
    return usage;
}

CliResult RunCli(const std::wstring &exe, const std::wstring &args, DWORD timeoutMs)
{
    CliResult result;
    SECURITY_ATTRIBUTES inherit = {sizeof inherit, nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 64 * 1024))      /* far more than these programs print */
        return result;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi = {};
    const bool started = Create(exe, args, SW_HIDE, writeEnd, &pi);
    CloseHandle(writeEnd);                              /* so the pipe ends when the program does */
    if (started) {
        CloseHandle(pi.hThread);
        if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            result.exitCode = static_cast<int>(code);
            char buffer[512];
            DWORD got = 0;
            while (ReadFile(readEnd, buffer, sizeof buffer, &got, nullptr) && got > 0)
                result.output.append(buffer, got);
            while (!result.output.empty() && (result.output.back() == '\n' || result.output.back() == '\r'))
                result.output.pop_back();
        } else {
            TerminateProcess(pi.hProcess, 1);
        }
        CloseHandle(pi.hProcess);
    }
    CloseHandle(readEnd);
    return result;
}

std::vector<DWORD> FindProcesses(const wchar_t *exeName)
{
    std::vector<DWORD> ids;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return ids;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof entry;
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, exeName) == 0)
            ids.push_back(entry.th32ProcessID);
    }
    CloseHandle(snapshot);
    return ids;
}

DWORD ForegroundProcessId()
{
    DWORD pid = 0;
    if (HWND window = GetForegroundWindow())
        GetWindowThreadProcessId(window, &pid);
    return pid;
}
