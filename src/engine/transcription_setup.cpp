#include "transcription_setup.h"

#include <utility>

namespace audiorgui {

TranscriptionSetup::TranscriptionSetup(Environment environment) : env_(std::move(environment)), declined_(env_.declined) {}

TranscriptionSetup::~TranscriptionSetup()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);       /* under the lock, or the wake-up could slip between a wait's check and its sleep */
        stop_ = true;
    }
    if (env_.cancelInstall)
        env_.cancelInstall();                           /* a download waiting on the network ends now */
    changed_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void TranscriptionSetup::Start() { Launch(false); }

void TranscriptionSetup::Launch(bool forced)
{
    if (thread_.joinable())
        thread_.join();                                 /* the last run has ended, or is about to (see Retry) */
    {
        std::lock_guard<std::mutex> lock(mutex_);      /* Scanning is said here, not by the thread: whoever polls next has something to see */
        reply_ = Reply::Pending;
        status_ = ScanningStatus();
        status_.declined = declined_;
        ++version_;
    }
    cancel_ = false;
    thread_ = std::thread([this, forced] {
        try {
            Run(forced);
        } catch (...) {                                 /* an out-of-memory, a driver's exception: the recording is not to end for it */
            try {
                Publish(FailedStatus(L"The set-up stopped", L"Something went wrong while looking at this PC. Recording is not affected."));
            } catch (...) {
            }
        }
    });
}

/* Only a run that has said its last word can be repeated. */
void TranscriptionSetup::Retry()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_ || (status_.phase != SetupPhase::Idle && status_.phase != SetupPhase::Finished))
            return;
    }
    Launch(true);
}

bool TranscriptionSetup::Poll(SetupStatus *out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (version_ == polled_)
        return false;
    *out = status_;
    polled_ = version_;
    return true;
}

void TranscriptionSetup::Answer(bool download)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_.phase != SetupPhase::Asking || reply_ != Reply::Pending)
        return;
    reply_ = download ? Reply::Yes : Reply::No;
    changed_.notify_all();
}

void TranscriptionSetup::Cancel()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.phase != SetupPhase::Downloading)
            return;
        cancel_ = true;
    }
    if (env_.cancelInstall)
        env_.cancelInstall();
}

void TranscriptionSetup::Publish(SetupStatus status)
{
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = std::move(status);
    status_.declined = declined_;
    ++version_;
}

/* Waits for the user's answer; false if the program is closing first. */
bool TranscriptionSetup::WaitForAnswer(Reply *reply)
{
    std::unique_lock<std::mutex> lock(mutex_);
    changed_.wait(lock, [this] { return reply_ != Reply::Pending || stop_; });
    *reply = reply_;
    return !stop_;
}

void TranscriptionSetup::Run(bool forced)
{
    const HardwareProfile hardware = env_.scan();
    const std::vector<const AsrModel *> installed = env_.installedModels();
    const SetupPlan plan = PlanSetup(hardware, env_.backendPresent, installed, env_.voiceModelInstalled(), env_.models,
                                     env_.modelCount, env_.voice);

    /* What every status from here on tells the program besides its words. */
    const auto stamped = [&](SetupStatus s) {
        s.scanned = true;
        s.gpuUsable = plan.gpu.use == GpuUse::Usable;
        return s;
    };
    const auto finish = [&](SetupStatus s, bool modelsChanged) {
        s = stamped(std::move(s));
        s.modelsChanged = modelsChanged;
        s.canRetry = env_.installedModels().empty();    /* nothing to use: the user may want to try again */
        Publish(std::move(s));
    };

    if (plan.download.empty()) {                        /* nothing is missing */
        finish(installed.empty() ? SetupStatus() : ReadyStatus(plan.gpu, installed.front()->label, false), false);
        return;
    }
    if (const std::wstring why = env_.downloadProblem(); !why.empty()) {
        finish(UnavailableStatus(why), false);
        return;
    }
    if (!forced && declined_) {                         /* said no before: no notification, no question (only this thread changes it) */
        finish(SetupStatus(), false);
        return;
    }

    Publish(stamped(AskingStatus(plan)));
    Reply reply = Reply::Pending;
    if (!WaitForAnswer(&reply))
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        declined_ = reply == Reply::No;                 /* a yes takes back an earlier no; a cancel later is not a no */
    }
    if (reply == Reply::No) {
        finish(SkippedStatus(false), false);
        return;
    }

    std::uint64_t before = 0;                           /* the bytes of the files that are done */
    bool any = false;
    for (std::size_t i = 0; i < plan.download.size(); ++i) {
        if (cancel_) {                                  /* ended between two files */
            finish(SkippedStatus(true, any), any);
            return;
        }
        const AsrModel &model = *plan.download[i];
        Publish(stamped(DownloadingStatus(plan, i, before)));
        const auto progress = [&](ModelInstaller::Stage stage, std::uint64_t bytes) {
            Publish(stamped(stage == ModelInstaller::Stage::Checking ? CheckingStatus(plan, i)
                                                                     : DownloadingStatus(plan, i, before + bytes)));
        };
        std::wstring problem;
        const bool ok = env_.install(model, progress, [this] { return StopRequested(); }, &problem);
        if (stop_)
            return;                                     /* closing: nobody is left to tell */
        if (ok) {                                       /* whole and in place, whatever came after: a late Cancel changes nothing */
            before += model.bytes;
            any = true;
            continue;
        }
        if (cancel_)
            finish(SkippedStatus(true, any), any);
        else
            finish(FailedStatus(L"Download failed", any ? problem + L"  A speech model that came first is installed and works." : problem), any);
        return;
    }

    const std::vector<const AsrModel *> now = env_.installedModels();
    finish(ReadyStatus(plan.gpu, now.empty() ? plan.download.front()->label : now.front()->label, true), true);
}

}  // namespace audiorgui
