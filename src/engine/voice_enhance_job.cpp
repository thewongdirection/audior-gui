#include "voice_enhance_job.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "../core/voice_enhancer.h"
#include "hresult.h"
#include "pcm.h"
#include "thread_support.h"

namespace audiorgui {

namespace {

const HRESULT kCancelled = HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);

constexpr float kToFloat = 1.0f / 32768.0f;

void ToFloat(const std::int16_t *in, std::size_t frames, float *out)
{
    for (std::size_t i = 0; i < 2 * frames; ++i)
        out[i] = static_cast<float>(in[i]) * kToFloat;
}

/* To 16 bits, rounded; the enhancer's limiter keeps the audio under full scale, the clamp is for the rounding. */
void ToPcm(const float *in, std::size_t frames, std::int16_t *out)
{
    for (std::size_t i = 0; i < 2 * frames; ++i)
        out[i] = static_cast<std::int16_t>(std::clamp(std::lrintf(in[i] * 32768.0f), -32768L, 32767L));
}

}  // namespace

VoiceEnhanceJob::VoiceEnhanceJob(ReaderFactory readers, SinkFactory sinks, std::vector<Task> tasks)
    : readers_(std::move(readers)), sinks_(std::move(sinks)), tasks_(std::move(tasks))
{
    thread_ = std::thread(&VoiceEnhanceJob::Work, this);
}

VoiceEnhanceJob::~VoiceEnhanceJob() { Cancel(); }

void VoiceEnhanceJob::Cancel()
{
    cancel_ = true;
    if (thread_.joinable())
        thread_.join();
}

EnhanceStatus VoiceEnhanceJob::Status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    EnhanceStatus status;
    status.state = state_;
    status.progress = state_ == EnhanceState::Done ? 1.0f : progress_.load();
    status.problem = problem_;
    return status;
}

std::vector<std::wstring> VoiceEnhanceJob::Done() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return done_;
}

void VoiceEnhanceJob::SetProgress(std::size_t index, std::uint64_t framesDone, std::uint64_t framesTotal)
{
    const double inFile = framesTotal == 0 ? 0.0 : std::min(1.0, static_cast<double>(framesDone) / static_cast<double>(framesTotal));
    progress_ = static_cast<float>((static_cast<double>(index) + inFile) / static_cast<double>(tasks_.size()));
}

void VoiceEnhanceJob::Work()
{
    ComScope com;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);   /* a recording or a transcription comes first */

    EnhanceState end = EnhanceState::Done;
    std::wstring problem;
    for (std::size_t i = 0; i < tasks_.size() && end == EnhanceState::Done; ++i) {
        const HRESULT hr = Run(tasks_[i], i);
        if (hr == kCancelled) {
            end = EnhanceState::Cancelled;
        } else if (FAILED(hr)) {
            end = EnhanceState::Failed;
            problem = WithHr(L"The voice could not be enhanced", hr);
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            done_.push_back(tasks_[i].target);
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = end;
    problem_ = problem;
}

/* One file: the first pass listens (VoiceAnalyzer), the second one enhances and writes.  The audio goes in blocks
 * of kBlockFrames through buffers that are made once; the two passes count together for the progress. */
HRESULT VoiceEnhanceJob::Run(const Task &task, std::size_t index)
{
    std::vector<std::int16_t> pcm(2 * kBlockFrames);
    std::vector<float>        block(2 * kBlockFrames), enhanced;
    std::uint64_t             done = 0, total = 0;

    /* Reads every block of `reader` and passes it on as floats; stops at the end, at an error, or when cancelled. */
    const auto forEachBlock = [&](IPcmReader &reader, const std::function<HRESULT(const float *, std::size_t)> &handle) {
        for (;;) {
            if (cancel_)
                return kCancelled;
            std::size_t frames = 0;
            HRESULT hr = reader.Read(pcm.data(), kBlockFrames, &frames);
            if (FAILED(hr) || frames == 0)
                return hr;
            ToFloat(pcm.data(), frames, block.data());
            hr = handle(block.data(), frames);
            if (FAILED(hr))
                return hr;
            done += frames;
            SetProgress(index, done, 2 * total);
        }
    };

    HRESULT hr = S_OK;
    std::unique_ptr<IPcmReader> first = readers_(task.source, &hr);
    if (!first)
        return FAILED(hr) ? hr : E_FAIL;
    total = first->TotalFrames();

    VoiceAnalyzer analyzer;
    hr = forEachBlock(*first, [&](const float *audio, std::size_t frames) {
        analyzer.Add(audio, frames);
        return S_OK;
    });
    first.reset();
    if (FAILED(hr))
        return hr;
    const VoiceProfile profile = analyzer.Finish();

    std::unique_ptr<IPcmReader> second = readers_(task.source, &hr);
    if (!second)
        return FAILED(hr) ? hr : E_FAIL;
    std::unique_ptr<IAudioSink> sink = sinks_(task.target, &hr);
    if (!sink)
        return FAILED(hr) ? hr : E_FAIL;

    VoiceEnhancer enhancer(profile);
    std::vector<std::int16_t> written;
    const auto write = [&]() {
        written.resize(enhanced.size());
        ToPcm(enhanced.data(), enhanced.size() / 2, written.data());
        enhanced.clear();
        return written.empty() ? S_OK : sink->Write(written.data(), written.size() * sizeof(std::int16_t));
    };
    hr = forEachBlock(*second, [&](const float *audio, std::size_t frames) {
        enhancer.Process(audio, frames, enhanced);
        return write();
    });
    if (SUCCEEDED(hr)) {
        enhancer.Finish(enhanced);
        hr = write();
    }
    if (FAILED(hr)) {
        sink->Close(false);                                 /* the unfinished copy goes */
        return hr;
    }
    hr = sink->Close(true);
    if (FAILED(hr))
        DeleteFileW(task.target.c_str());                   /* the sink made it (it refuses an existing file) */
    return hr;
}

}  // namespace audiorgui
