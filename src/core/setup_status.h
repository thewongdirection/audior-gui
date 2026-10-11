/*
 * setup_status.h - What the start-up set-up of live transcription tells the user.
 *
 * Right after start the program looks at this PC (graphics card, speech models),
 * and if no speech model is installed it offers to download one.  That work goes
 * through these phases:
 *
 *     Scanning -> Asking -> Downloading -> Finished          (a model is missing, and the user agrees)
 *     Scanning -> Finished                                   (nothing is missing; or the user declines)
 *
 * A SetupStatus is what the window's notification shows for the phase: a title, a
 * line or two of detail and, while downloading, how far it has got.  The builders
 * below make those texts, so the real set-up (engine/transcription_setup) and the
 * screenshots show exactly the same words.  Plain C++, no Windows.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "setup_plan.h"

namespace audiorgui {

enum class SetupPhase { Idle, Scanning, Asking, Downloading, Finished };

/* How a Finished set-up ended. */
enum class SetupResult { None, Ready, Skipped, Failed };

struct SetupStatus {
    SetupPhase   phase  = SetupPhase::Idle;
    SetupResult  result = SetupResult::None;
    std::wstring title;
    std::wstring detail;
    float        fraction = -1.0f;          /* Downloading: how much is done, 0 to 1; negative: not known */

    /* What the program does about it (see RecorderController): */
    bool scanned       = false;             /* the hardware scan is done ...                                    */
    bool gpuUsable     = true;              /* ... and found no card to use: the GPU is not used (the saved choice stays) */
    bool modelsChanged = false;             /* new files are in the models folder: look again                   */
    bool canRetry      = false;             /* no model is installed: offer the download again                  */
    bool declined      = false;             /* the user's "not now" stands (every status says, so the program can keep it) */
};

/* The set-up as seen from the window.  The set-up runs on its own thread; these are called on the UI thread. */
class ISetupSource {
public:
    virtual ~ISetupSource() = default;

    /* Copies the status into *out if it changed since the last call, and says whether it did. */
    virtual bool Poll(SetupStatus *out) = 0;

    /* The user's answer to the question of the Asking phase. */
    virtual void Answer(bool download) = 0;

    /* Ends a download in progress. */
    virtual void Cancel() = 0;

    /* Starts again, asking even if the user has declined before. */
    virtual void Retry() = 0;
};

/* The texts of each phase.  `plan` is what the scan decided; `file` counts from 0 in plan.download, and
 * `done` is how many bytes of all the plan's files have arrived. */
SetupStatus ScanningStatus();
SetupStatus AskingStatus(const SetupPlan &plan);
SetupStatus DownloadingStatus(const SetupPlan &plan, std::size_t file, std::uint64_t done);
SetupStatus CheckingStatus(const SetupPlan &plan, std::size_t file);

/* Finished.  `model` names the model that will be used (the first one installed). */
SetupStatus ReadyStatus(const GpuVerdict &gpu, const std::wstring &model, bool downloaded);
SetupStatus SkippedStatus(bool cancelled);
SetupStatus UnavailableStatus(const std::wstring &why);          /* a model is missing, and cannot be downloaded */
SetupStatus FailedStatus(const std::wstring &title, const std::wstring &problem);

}  // namespace audiorgui
