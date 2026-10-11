/*
 * process_transport.h - Runs the transcription helper as a child process and
 * talks to it over its standard input and output.
 *
 * The helper is not trusted with the recorder's fate, so it is started with as
 * little as possible:
 *
 *   - by absolute path, with its own folder as the working directory;
 *   - at below-normal priority, so it never competes with the capture threads;
 *   - inside a Job object that kills it when the recorder lets go (or dies),
 *     allows it no child processes, caps its memory and keeps Windows' crash
 *     dialog from appearing for it;
 *   - with exactly three handles inherited: its two pipes and the null device
 *     for its error output.
 *
 * Nothing but kernel32 is needed.
 */
#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

#include "transcription_host.h"

namespace audiorgui {

class ProcessTransport final : public IAsrTransport {
public:
    struct Limits {
        std::uint64_t memoryBytes = 6ull << 30;     /* what the helper may commit (model and buffers); a
                                                       32-bit recorder can only express a little under 2 GB */
    };

    /* Starts `exePath` (an absolute path) with `arguments` (may be empty).  Null,
     * with the reason in *problem, if it cannot be started. */
    static std::unique_ptr<ProcessTransport> Launch(const std::wstring &exePath, const std::wstring &arguments,
                                                    const Limits &limits, std::wstring *problem);

    ~ProcessTransport() override;
    ProcessTransport(const ProcessTransport &) = delete;
    ProcessTransport &operator=(const ProcessTransport &) = delete;

    bool        Write(const void *data, std::size_t count) override;
    std::size_t Read(void *buffer, std::size_t capacity) override;
    void        Close() override;                   /* kills the helper; frees a waiting Read or Write */

    bool  Running() const;
    DWORD ProcessId() const { return processId_; }

private:
    ProcessTransport() = default;

    HANDLE process_ = nullptr, job_ = nullptr;
    HANDLE toHelper_ = nullptr, fromHelper_ = nullptr;
    DWORD  processId_ = 0;
};

}  // namespace audiorgui
