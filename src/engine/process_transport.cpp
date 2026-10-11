#include "process_transport.h"

#include <algorithm>
#include <vector>

#include "hresult.h"

namespace audiorgui {

namespace {

constexpr DWORD kPipeBytes = 64 * 1024;             /* the pipe the audio goes through */
constexpr DWORD kExitWaitMs = 3000;                 /* how long the destructor waits for a killed helper */

/* Closes a handle when it goes out of scope. */
class Handle {
public:
    Handle() = default;
    ~Handle() { Reset(); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    HANDLE *Out() { Reset(); return &h_; }
    HANDLE  Get() const { return h_; }
    explicit operator bool() const { return h_ && h_ != INVALID_HANDLE_VALUE; }
    HANDLE  Release() { HANDLE h = h_; h_ = nullptr; return h; }
    void    Reset()
    {
        if (*this)
            CloseHandle(h_);
        h_ = nullptr;
    }

private:
    HANDLE h_ = nullptr;
};

bool IsAbsolute(const std::wstring &path)
{
    return (path.size() > 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
           (path.size() > 2 && path[0] == L'\\' && path[1] == L'\\');
}

std::wstring Failed(const wchar_t *what)
{
    return WithHr(what, static_cast<long>(HRESULT_FROM_WIN32(GetLastError())));
}

}  // namespace

std::unique_ptr<ProcessTransport> ProcessTransport::Launch(const std::wstring &exePath, const std::wstring &arguments,
                                                           const Limits &limits, std::wstring *problem)
{
    if (!IsAbsolute(exePath)) {
        *problem = L"The transcription engine's location is not a full path.";
        return nullptr;
    }

    /* Its standard input and output: pipes whose parent ends are not inherited. */
    Handle helperIn, parentOut, parentIn, helperOut, nul;
    SECURITY_ATTRIBUTES inheritable = {sizeof(inheritable), nullptr, TRUE};
    if (!CreatePipe(helperIn.Out(), parentOut.Out(), &inheritable, kPipeBytes) ||
        !CreatePipe(parentIn.Out(), helperOut.Out(), &inheritable, kPipeBytes) ||
        !SetHandleInformation(parentOut.Get(), HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(parentIn.Get(), HANDLE_FLAG_INHERIT, 0)) {
        *problem = Failed(L"The transcription engine could not be started");
        return nullptr;
    }
    *nul.Out() = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING,
                             0, nullptr);
    if (!nul) {
        *problem = Failed(L"The transcription engine could not be started");
        return nullptr;
    }

    /* Only those three handles are passed on. */
    SIZE_T listBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &listBytes);
    std::vector<char> listStorage(listBytes);
    LPPROC_THREAD_ATTRIBUTE_LIST list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(listStorage.data());
    HANDLE inherited[3] = {helperIn.Get(), helperOut.Get(), nul.Get()};
    if (!InitializeProcThreadAttributeList(list, 1, 0, &listBytes)) {
        *problem = Failed(L"The transcription engine could not be started");
        return nullptr;
    }
    const bool listed = UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                                  sizeof(inherited), nullptr, nullptr) != FALSE;

    STARTUPINFOEXW startup = {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = helperIn.Get();
    startup.StartupInfo.hStdOutput = helperOut.Get();
    startup.StartupInfo.hStdError = nul.Get();
    startup.lpAttributeList = list;

    std::wstring command = L"\"" + exePath + L"\"" + (arguments.empty() ? L"" : L" " + arguments);
    const std::wstring directory = exePath.substr(0, exePath.find_last_of(L"\\/"));
    PROCESS_INFORMATION info = {};
    const BOOL created = listed && CreateProcessW(exePath.c_str(), &command[0], nullptr, nullptr, TRUE,
                                                  CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT |
                                                      BELOW_NORMAL_PRIORITY_CLASS,
                                                  nullptr, directory.c_str(), &startup.StartupInfo, &info);
    const std::wstring createError = created ? std::wstring() : Failed(L"The transcription engine could not be started");
    DeleteProcThreadAttributeList(list);
    if (!created) {
        *problem = createError;
        return nullptr;
    }

    std::unique_ptr<ProcessTransport> transport(new ProcessTransport());
    transport->process_ = info.hProcess;                /* from here on the destructor cleans up */
    transport->processId_ = info.dwProcessId;
    Handle thread;
    *thread.Out() = info.hThread;

    /* The job: one process, a memory cap, no crash dialog, and gone when the recorder lets go. */
    transport->job_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION cap = {};
    cap.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
                                           JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    cap.BasicLimitInformation.ActiveProcessLimit = 1;
    cap.ProcessMemoryLimit = static_cast<SIZE_T>(std::min<std::uint64_t>(limits.memoryBytes, SIZE_MAX / 2));
    if (!transport->job_ ||
        !SetInformationJobObject(transport->job_, JobObjectExtendedLimitInformation, &cap, sizeof(cap)) ||
        !AssignProcessToJobObject(transport->job_, transport->process_) || ResumeThread(thread.Get()) == static_cast<DWORD>(-1)) {
        *problem = Failed(L"The transcription engine could not be started");
        TerminateProcess(transport->process_, 1);       /* it never ran: do not leave it suspended */
        return nullptr;
    }

    transport->toHelper_ = parentOut.Release();
    transport->fromHelper_ = parentIn.Release();
    return transport;                                   /* the helper's own ends close as the Handles go */
}

ProcessTransport::~ProcessTransport()
{
    Close();
    if (process_) {
        WaitForSingleObject(process_, kExitWaitMs);
        CloseHandle(process_);
    }
    if (job_)
        CloseHandle(job_);
    if (toHelper_)
        CloseHandle(toHelper_);
    if (fromHelper_)
        CloseHandle(fromHelper_);
}

bool ProcessTransport::Write(const void *data, std::size_t count)
{
    const char *p = static_cast<const char *>(data);
    while (count > 0) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(count, 1u << 20));
        if (!toHelper_ || !WriteFile(toHelper_, p, chunk, &written, nullptr) || written == 0)
            return false;
        p += written;
        count -= written;
    }
    return true;
}

std::size_t ProcessTransport::Read(void *buffer, std::size_t capacity)
{
    for (;;) {
        DWORD got = 0;
        if (!fromHelper_ || !ReadFile(fromHelper_, buffer, static_cast<DWORD>(std::min<std::size_t>(capacity, 1u << 20)),
                                      &got, nullptr))
            return 0;                                   /* the helper exited (or was killed): a broken pipe */
        if (got > 0)
            return got;                                 /* (an empty write is not the end of the stream) */
    }
}

/* Killing the helper closes its ends of both pipes, which frees a Read or Write waiting on them. */
void ProcessTransport::Close()
{
    if (job_)
        TerminateJobObject(job_, 1);
}

bool ProcessTransport::Running() const
{
    return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

}  // namespace audiorgui
