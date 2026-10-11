#include "fake_asr_helper.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/core/asr_protocol.h"

using namespace audiorgui;

namespace {

void Send(HANDLE out, const asr::Message &message)
{
    std::vector<std::uint8_t> frame(asr::kMaxFrame);
    const std::size_t size = asr::Encode(message, frame.data(), frame.size());
    DWORD written = 0;
    if (size > 0)
        WriteFile(out, frame.data(), static_cast<DWORD>(size), &written, nullptr);
}

/* Whether this process can start another one (the recorder's job allows none). */
const wchar_t *TrySpawn()
{
    wchar_t shell[MAX_PATH], command[MAX_PATH + 16];
    if (GetEnvironmentVariableW(L"ComSpec", shell, MAX_PATH) == 0)
        return L"spawn=unknown";
    std::swprintf(command, sizeof(command) / sizeof(command[0]), L"\"%ls\" /c exit", shell);
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION info = {};
    if (!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info))
        return L"spawn=blocked";
    WaitForSingleObject(info.hProcess, 5000);
    DWORD exitCode = 1;
    GetExitCodeProcess(info.hProcess, &exitCode);       /* a process over the job's limit is created, then killed */
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    return exitCode == 0 ? L"spawn=ok" : L"spawn=blocked";
}

}  // namespace

int RunFakeAsrHelper(const char *mode)
{
    const std::string m = mode;
    if (m == "crash")
        return 3;
    if (m == "hang") {
        Sleep(120000);
        return 0;
    }

    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
    asr::MessageReader reader;
    asr::Message message;
    std::uint8_t bytes[4096];
    std::uint64_t samples = 0;

    for (;;) {
        DWORD n = 0;
        if (!ReadFile(in, bytes, sizeof(bytes), &n, nullptr) || n == 0)
            return 0;                                   /* the recorder let go */
        for (DWORD used = 0; used < n;) {
            used += static_cast<DWORD>(reader.Append(bytes + used, n - used));
            while (reader.Next(&message) == asr::MessageReader::Result::Message) {
                if (message.type == asr::Type::Hello) {
                    wchar_t text[96];
                    std::swprintf(text, sizeof(text) / sizeof(text[0]), L"priority=%lx %ls",
                                  GetPriorityClass(GetCurrentProcess()), m == "spawn" ? TrySpawn() : L"");
                    asr::Message ready;
                    ready.type = asr::Type::Ready;
                    ready.text = text;
                    Send(out, ready);
                } else if (message.type == asr::Type::Audio || message.type == asr::Type::Stop) {
                    const bool stop = message.type == asr::Type::Stop;
                    samples += message.sampleCount;
                    asr::Message event;
                    event.type = asr::Type::Event;
                    event.stream = message.stream;
                    event.event.utterance = 0;
                    event.event.isFinal = stop;
                    const std::wstring text = stop ? L"bye" : L"samples=" + std::to_wstring(samples);
                    event.event.SetText(text.c_str(), text.size());
                    Send(out, event);
                    if (stop)
                        return 0;
                }
            }
        }
    }
}
