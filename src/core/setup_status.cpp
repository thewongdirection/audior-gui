#include "setup_status.h"

#include <algorithm>
#include <utility>

#include "format.h"

namespace audiorgui {

namespace {

SetupStatus Make(SetupPhase phase, SetupResult result, std::wstring title, std::wstring detail)
{
    SetupStatus s;
    s.phase = phase;
    s.result = result;
    s.title = std::move(title);
    s.detail = std::move(detail);
    return s;
}

/* "A", "A and B", "A, B and C". */
std::wstring Names(const SetupPlan &plan)
{
    std::wstring text;
    for (std::size_t i = 0; i < plan.download.size(); ++i) {
        if (i > 0)
            text += i + 1 == plan.download.size() ? L" and " : L", ";
        text += plan.download[i]->label;
    }
    return text;
}

}  // namespace

SetupStatus ScanningStatus()
{
    return Make(SetupPhase::Scanning, SetupResult::None, L"Checking this PC",
                L"Looking for a graphics card and speech models…");
}

SetupStatus AskingStatus(const SetupPlan &plan)
{
    return Make(SetupPhase::Asking, SetupResult::None, L"Live transcripts need a speech model",
                L"Download " + Names(plan) + L" (" + FormatBytes(plan.bytes) + L") from huggingface.co? " +
                    DescribeGpu(plan.gpu));
}

SetupStatus DownloadingStatus(const SetupPlan &plan, std::size_t file, std::uint64_t done)
{
    done = std::min(done, plan.bytes);
    std::wstring title = L"Downloading " + std::wstring(plan.download[file]->label);
    if (plan.download.size() > 1)
        title += L"  (" + std::to_wstring(file + 1) + L" of " + std::to_wstring(plan.download.size()) + L")";

    SetupStatus s = Make(SetupPhase::Downloading, SetupResult::None, std::move(title), FormatBytes(done) + L" of " + FormatBytes(plan.bytes));
    if (plan.bytes > 0) {
        s.fraction = static_cast<float>(static_cast<double>(done) / static_cast<double>(plan.bytes));
        s.detail += L"  ·  " + std::to_wstring(static_cast<int>(s.fraction * 100.0f)) + L"%";
    }
    return s;
}

SetupStatus CheckingStatus(const SetupPlan &plan, std::size_t file)
{
    return Make(SetupPhase::Downloading, SetupResult::None, L"Checking " + std::wstring(plan.download[file]->label),
                L"Making sure the file is the one that was published…");
}

SetupStatus ReadyStatus(const GpuVerdict &gpu, const std::wstring &model, bool downloaded)
{
    return Make(SetupPhase::Finished, SetupResult::Ready,
                downloaded ? L"Speech model installed" : L"Live transcripts are ready",
                DescribeGpu(gpu) + L"  Model: " + model + L".");
}

SetupStatus SkippedStatus(bool cancelled, bool modelInstalled)
{
    return Make(SetupPhase::Finished, SetupResult::Skipped, cancelled ? L"Download cancelled" : L"Download skipped",
                modelInstalled ? L"A speech model came first and is installed: live transcripts work. The rest was not downloaded."
                               : L"Live transcripts stay off until a speech model is installed. The Transcript section can download "
                                 L"one later.");
}

SetupStatus UnavailableStatus(const std::wstring &why)
{
    return Make(SetupPhase::Finished, SetupResult::Skipped, L"No speech model installed", why);
}

SetupStatus FailedStatus(const std::wstring &title, const std::wstring &problem)
{
    return Make(SetupPhase::Finished, SetupResult::Failed, title, problem);
}

}  // namespace audiorgui
