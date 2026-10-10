#include "peak_tap.h"

#include <algorithm>

namespace audiorgui {

void PeakAccumulator::Add(const std::int16_t *samples, std::size_t frames, std::vector<PeakBin> *bins)
{
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t c = 0; c < kPcmChannels; ++c) {
            const std::int16_t s = samples[f * kPcmChannels + c];
            low_  = std::min(low_, s);
            high_ = std::max(high_, s);
        }
        if (++filled_ == kFramesPerPeakBin) {
            PeakBin bin;
            bin.low  = static_cast<float>(low_) / 32768.0f;
            bin.high = static_cast<float>(high_) / 32768.0f;
            bins->push_back(bin);
            filled_ = 0;
            low_ = high_ = 0;
        }
    }
}

void PeakQueue::Push(const PeakBin *bins, std::size_t count)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < count; ++i) {
        if (used_ == bins_.size()) {            /* full: drop the oldest */
            head_ = (head_ + 1) % bins_.size();
            --used_;
        }
        bins_[(head_ + used_) % bins_.size()] = bins[i];
        ++used_;
    }
}

std::size_t PeakQueue::Take(PeakBin *out, std::size_t capacity)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t n = std::min(capacity, used_);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = bins_[(head_ + i) % bins_.size()];
    head_ = (head_ + n) % bins_.size();
    used_ -= n;
    return n;
}

void PeakQueue::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    head_ = used_ = 0;
}

}  // namespace audiorgui
