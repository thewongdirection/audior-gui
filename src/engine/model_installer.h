/*
 * model_installer.h - Puts one speech model into the models folder: downloaded
 * by the download program, checked, then moved into place.
 *
 *   <models>\<file>.part   what audior_fetch.exe writes (it creates this file and no other)
 *   size and SHA-256       checked here, against the table of models, before anything is
 *                          used: the download program is trusted with the network and
 *                          nothing else
 *   <models>\<file>        where it is moved to once both match
 *
 * So a half-finished download, a wrong file or one tampered with on the way never
 * gets a name the recorder would use, and a failed or cancelled try leaves no
 * .part file behind.  Nothing here touches the network itself: see
 * core/fetch_protocol.h for what the download program says.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "../core/asr_models.h"
#include "transcription_host.h"

namespace audiorgui {

class ProcessTransport;

class ModelInstaller {
public:
    enum class Stage { Downloading, Checking };

    /* How far it is: bytes written so far while Downloading (a few times a second at most), 0 for Checking. */
    using Progress = std::function<void(Stage stage, std::uint64_t bytes)>;

    struct Options {
        std::wstring modelsDir;
        std::wstring fetcher;                           /* the download program's full path                       */
        std::wstring fetcherArguments;                  /* put before the three it is always given (for a stand-in) */
        std::function<std::wstring()> verify;           /* why it must not be started, or empty (null: no check) */
    };

    explicit ModelInstaller(Options options) : options_(std::move(options)) {}

    /* Downloads `model` into the models folder, whole and checked, or not at all.  It blocks for as long as
     * the download takes.  False with the reason in *problem if it could not, or if `stopping` became true
     * (see Cancel). */
    bool Install(const AsrModel &model, const Progress &progress, const StopCheck &stopping, std::wstring *problem);

    /* From any thread: ends the download program, so that an Install waiting for it returns.  (It is
     * `stopping` that says the Install is to end; this just does not make it wait for the network.) */
    void Cancel();

private:
    Options          options_;
    std::mutex       mutex_;
    ProcessTransport *running_ = nullptr;               /* the download program, while one runs (guarded by mutex_) */
};

}  // namespace audiorgui
