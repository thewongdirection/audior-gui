/*
 * voice_enhance_job.h - Writes the clearer copies of a finished recording, in the background.
 *
 * For each file: read it once for the analysis, read it again through the enhancer (core/voice_enhancer.h) into a
 * new file beside it.  The recording is never touched and the copy never replaces a file (the sink refuses), so
 * whatever goes wrong, the recording is as it was.  The work runs on a thread of its own at below-normal priority;
 * memory use is a few blocks of audio however long the recording is.
 *
 * A copy that is not finished (cancelled, failed) is removed.  Readers and sinks come from factories, so the whole
 * job is tested with generated audio and in-memory files.
 */
#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/engine.h"
#include "audio_sink.h"
#include "pcm_reader.h"

namespace audiorgui {

class VoiceEnhanceJob {
public:
    using ReaderFactory = std::function<std::unique_ptr<IPcmReader>(const std::wstring &path, HRESULT *hr)>;
    using SinkFactory   = std::function<std::unique_ptr<IAudioSink>(const std::wstring &path, HRESULT *hr)>;

    struct Task {
        std::wstring source;            /* the recording                      */
        std::wstring target;            /* the clearer copy, not yet there    */
    };

    /* Starts at once. */
    VoiceEnhanceJob(ReaderFactory readers, SinkFactory sinks, std::vector<Task> tasks);

    /* Cancels what is left to do and waits for the thread. */
    ~VoiceEnhanceJob();

    VoiceEnhanceJob(const VoiceEnhanceJob &) = delete;
    VoiceEnhanceJob &operator=(const VoiceEnhanceJob &) = delete;

    /* Gives up what is not finished and waits until the thread has ended; the copy it was writing is removed.
     * Does nothing to a job that has ended. */
    void Cancel();

    EnhanceStatus             Status() const;
    std::vector<std::wstring> Done() const;         /* the copies that are complete */

    /* Frames per block of audio read, processed and written. */
    static constexpr std::size_t kBlockFrames = 16384;

private:
    HRESULT Run(const Task &task, std::size_t index);
    void    Work();
    void    SetProgress(std::size_t index, std::uint64_t framesDone, std::uint64_t framesTotal);

    ReaderFactory     readers_;
    SinkFactory       sinks_;
    std::vector<Task> tasks_;

    std::atomic<bool>  cancel_{false};
    std::atomic<float> progress_{0.0f};

    mutable std::mutex        mutex_;               /* guards what follows */
    EnhanceState              state_ = EnhanceState::Working;
    std::wstring              problem_;
    std::vector<std::wstring> done_;

    std::thread thread_;                            /* last: started once everything else is set up */
};

}  // namespace audiorgui
