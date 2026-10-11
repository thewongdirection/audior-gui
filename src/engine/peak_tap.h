/*
 * peak_tap.h - Reports what is being recorded to the waveform.
 *
 * A capture thread feeds the canonical audio it has just converted (so after
 * the microphone gain, and before any merging) to a PeakAccumulator, which
 * boils it down to one PeakBin per 10 ms.  The bins cross to the UI thread
 * through a PeakQueue.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "../core/types.h"
#include "pcm.h"

namespace audiorgui {

class PeakAccumulator {
public:
    /* Adds `frames` interleaved stereo frames; appends a bin for every
     * kFramesPerPeakBin frames completed.  A partly filled bin carries over. */
    void Add(const std::int16_t *samples, std::size_t frames, std::vector<PeakBin> *bins);

private:
    std::uint32_t filled_ = 0;
    std::int16_t  low_ = 0, high_ = 0;
};

/* A thread-safe hand-over of peak bins.  It keeps only the newest `capacity`
 * bins: a UI that stops asking loses the oldest, it never grows the queue. */
class PeakQueue {
public:
    explicit PeakQueue(std::size_t capacity = 512) : bins_(capacity) {}

    void        Push(const PeakBin *bins, std::size_t count);
    std::size_t Take(PeakBin *out, std::size_t capacity);      /* oldest first */
    void        Clear();

private:
    std::mutex           mutex_;
    std::vector<PeakBin> bins_;
    std::size_t          head_ = 0, used_ = 0;
};

}  // namespace audiorgui
