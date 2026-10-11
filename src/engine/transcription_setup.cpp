#include "transcription_setup.h"

#include <utility>

namespace audiorgui {

TranscriptionSetup::TranscriptionSetup(Environment environment) : env_(std::move(environment)) {}

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
        thread_.join();                                 /* the last run has ended (see Retry) */
    {
        std::lock_guard<std::mutex> lock(mutex_);
        reply_ = Reply::Pending;
    }
    cancel_ = false;
    running_ = true;
    thread_ = std::thread([this, forced] {
        Run(forced);
        running_ = false;
    });
}

void TranscriptionSetup::Retry()
{
    if (!running_ && !stop_)
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
    Publish(ScanningStatus());
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
    if (!forced && env_.declined()) {                   /* said no before: no notification, no question */
        finish(SetupStatus(), false);
        return;
    }

    Publish(stamped(AskingStatus(plan)));
    Reply reply = Reply::Pending;
    if (!WaitForAnswer(&reply))
        return;
    env_.rememberDecline(reply == Reply::No);
    if (reply == Reply::No) {
        finish(SkippedStatus(false), false);
        return;
    }

    std::uint64_t before = 0;                           /* the bytes of the files that are done */
    bool any = false;
    for (std::size_t i = 0; i < plan.download.size(); ++i) {
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
        if (cancel_) {
            finish(SkippedStatus(true), any);
            return;
        }
        if (!ok) {
            finish(FailedStatus(L"Download failed", problem), any);
            return;
        }
        before += model.bytes;
        any = true;
    }

    const std::vector<const AsrModel *> now = env_.installedModels();
    finish(ReadyStatus(plan.gpu, now.empty() ? plan.download.front()->label : now.front()->label, true), true);
}

}  // namespace audiorgui
