/*
 * transcription_setup.h - The start-up set-up of live transcription: look at the
 * PC, find out whether a speech model is installed and, if none is, offer to
 * download one.
 *
 *   thread of its own                             UI thread
 *   -----------------                             ---------
 *   scan the hardware, list the models
 *   a model is installed ........................ the notification says so, and which card will be used
 *   none is installed: ask ...................... the notification asks; Answer(true | false)
 *   download, check, move into place ............ the notification shows how far it is; Cancel()
 *   done ........................................ the notification says so; the recorder looks again for models
 *
 * Nothing is downloaded without the user's yes.  A "no" is remembered, so the
 * question is not asked at every start (Retry asks again).  If the user declines
 * or anything fails, transcription simply makes do with what is installed.
 *
 * All the doing is behind the functions of Environment, so the tests run the
 * whole conversation with made-up hardware, models and downloads, and the real ones
 * are wired up in system_setup.cpp.  This file has no Windows code.
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/setup_status.h"
#include "model_installer.h"

namespace audiorgui {

class TranscriptionSetup final : public ISetupSource {
public:
    struct Environment {
        std::function<HardwareProfile()>               scan;
        bool                                           backendPresent = false;     /* the helper can use a graphics card */
        std::function<std::vector<const AsrModel *>()> installedModels;            /* the models that are whole in the folder */
        std::function<bool()>                          voiceModelInstalled;

        /* Why a download cannot be started (the download program is missing, say), or empty. */
        std::function<std::wstring()> downloadProblem;

        /* Downloads one model into the folder, whole and checked (see ModelInstaller::Install).  Blocks. */
        std::function<bool(const AsrModel &, const ModelInstaller::Progress &, const StopCheck &, std::wstring *)> install;
        std::function<void()> cancelInstall;            /* from any thread: ends an install that is waiting */

        std::function<bool()>     declined;             /* the user said no before, and that was remembered */
        std::function<void(bool)> rememberDecline;

        const AsrModel *models     = kAsrModels;        /* the table (the tests give it a small one) */
        std::size_t     modelCount = kAsrModelCount;
        const AsrModel *voice      = &kVadModel;
    };

    explicit TranscriptionSetup(Environment environment);
    ~TranscriptionSetup() override;                     /* stops the thread, and a download in progress */

    TranscriptionSetup(const TranscriptionSetup &) = delete;
    TranscriptionSetup &operator=(const TranscriptionSetup &) = delete;

    /* Begins the first run, on a thread of its own; returns at once. */
    void Start();

    /* ISetupSource (UI thread) */
    bool Poll(SetupStatus *out) override;
    void Answer(bool download) override;
    void Cancel() override;
    void Retry() override;

private:
    enum class Reply { Pending, Yes, No };

    void Launch(bool forced);
    void Run(bool forced);
    void Publish(SetupStatus status);
    bool WaitForAnswer(Reply *reply);
    bool StopRequested() const { return stop_ || cancel_; }

    Environment env_;

    std::mutex              mutex_;                     /* guards what follows, up to the flags */
    std::condition_variable changed_;
    SetupStatus             status_;
    std::uint32_t           version_ = 0, polled_ = 0;
    Reply                   reply_ = Reply::Pending;

    std::atomic<bool> stop_{false};                     /* the program is closing */
    std::atomic<bool> cancel_{false};                   /* the user ended the download */
    std::atomic<bool> running_{false};
    std::thread       thread_;
};

}  // namespace audiorgui
