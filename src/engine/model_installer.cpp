#include "model_installer.h"

#include <windows.h>

#include <memory>

#include "../core/fetch_protocol.h"
#include "../core/settings.h"
#include "asr_helper.h"
#include "process_transport.h"

namespace audiorgui {

namespace {

constexpr std::uint64_t kSpareBytes = 64ull << 20;          /* free space to leave on the disk besides the model */
constexpr std::uint64_t kFetcherMemoryBytes = 256ull << 20; /* what the download program may commit */

/* Creates `dir`, and the folders above it that are missing. */
bool EnsureFolder(const std::wstring &dir)
{
    if (CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS)
        return true;
    const std::size_t slash = dir.find_last_of(L'\\');
    if (GetLastError() != ERROR_PATH_NOT_FOUND || slash == std::wstring::npos || slash == 0 ||
        !EnsureFolder(dir.substr(0, slash)))
        return false;
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

}  // namespace

void ModelInstaller::Cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        running_->Close();
}

bool ModelInstaller::Install(const AsrModel &model, const Progress &progress, const StopCheck &stopping,
                             std::wstring *problem)
{
    const std::wstring name = model.label;
    const auto stopped = [&] { return stopping && stopping(); };
    const auto report = [&](Stage stage, std::uint64_t bytes) {
        if (progress)
            progress(stage, bytes);
    };
    if (stopped()) {
        *problem = L"The download was cancelled.";
        return false;
    }
    /* The folder is checked before a single path is made from it, so that nothing is ever written, or deleted, anywhere else. */
    if (options_.modelsDir.empty()) {
        *problem = L"There is no folder to keep the speech models in.";
        return false;
    }
    if (!IsAbsolutePath(options_.modelsDir) || options_.modelsDir.find(L'"') != std::wstring::npos) {
        *problem = L"The folder for the speech models (" + options_.modelsDir + L") is not a full path.";    /* or its quote would break the arguments */
        return false;
    }

    const std::wstring path = JoinPath(options_.modelsDir, model.file), part = path + L".part";
    const auto fail = [&](const std::wstring &why) {
        DeleteFileW(part.c_str());                          /* no half-finished file is left behind */
        *problem = why;
        return false;
    };
    if (options_.verify) {
        const std::wstring why = options_.verify();
        if (!why.empty())
            return fail(why);
    }
    if (!EnsureFolder(options_.modelsDir))
        return fail(L"The folder " + options_.modelsDir + L" could not be created.");
    ULARGE_INTEGER available = {};
    if (GetDiskFreeSpaceExW(options_.modelsDir.c_str(), &available, nullptr, nullptr) &&
        available.QuadPart < model.bytes + kSpareBytes)
        return fail(L"There is not enough free disk space for " + name + L" (" + std::to_wstring((model.bytes + kSpareBytes) >> 20) +
                    L" MB needed in " + options_.modelsDir + L").");
    DeleteFileW(part.c_str());                              /* what an earlier try left */

    /* The download program, which creates `part` and fills it. */
    const std::wstring arguments = (options_.fetcherArguments.empty() ? std::wstring() : options_.fetcherArguments + L" ") +
                                   L"\"" + ModelUrl(model) + L"\" \"" + part + L"\" " + std::to_wstring(model.bytes);
    ProcessTransport::Limits limits;
    limits.memoryBytes = kFetcherMemoryBytes;
    std::wstring launchProblem;
    std::unique_ptr<ProcessTransport> fetcher = ProcessTransport::Launch(options_.fetcher, arguments, limits, &launchProblem);
    if (!fetcher)
        return fail(L"The download program could not be started: " + launchProblem);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = fetcher.get();
    }
    if (stopped())                                          /* asked to stop before it was registered to be cancelled */
        fetcher->Close();

    bool done = false;
    std::wstring reason;
    fetch::LineReader reader;
    char bytes[512];
    for (std::size_t n = 0; (n = fetcher->Read(bytes, sizeof(bytes))) > 0;) {
        reader.Append(bytes, n, [&](const fetch::Line &line) {
            switch (line.kind) {
            case fetch::Line::Kind::Progress:
                report(Stage::Downloading, line.bytes);
                break;
            case fetch::Line::Kind::Done:
                done = true;
                break;
            case fetch::Line::Kind::Error:
                reason = line.text;
                break;
            }
        });
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = nullptr;
    }
    fetcher.reset();                                        /* it has ended, and let go of the file, before the file is read */

    if (stopped())
        return fail(L"The download was cancelled.");
    if (!done)
        return fail(L"The download of " + name + L" did not finish: " +
                    (reason.empty() ? std::wstring(L"the download program stopped") : reason) + L".");

    /* Whatever the download program says, only the file the table describes is kept. */
    report(Stage::Checking, 0);
    std::uint64_t size = 0;
    if (!FileSize(part, &size) || size != model.bytes)
        return fail(L"The downloaded " + name + L" is not the size it should be.");
    std::string hash;
    if (!HashFile(part, &hash, stopping))
        return fail(stopped() ? std::wstring(L"The download was cancelled.")
                               : L"The downloaded " + name + L" could not be read to check it.");
    if (hash != model.sha256)
        return fail(L"The downloaded " + name + L" is not the file that was published (its SHA-256 differs).");
    if (!MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        return fail(L"The downloaded " + name + L" could not be moved into " + options_.modelsDir + L".");
    return true;
}

}  // namespace audiorgui
