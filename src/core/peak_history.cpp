#include "peak_history.h"

#include <algorithm>

namespace audiorgui {

PeakHistory::PeakHistory(std::size_t capacityBins)
    : ring_(std::max<std::size_t>(capacityBins, 1))
{
}

void PeakHistory::Clear()
{
    std::fill(ring_.begin(), ring_.end(), PeakBin());
    total_ = 0;
}

void PeakHistory::Append(const PeakBin *bins, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
        ring_[static_cast<std::size_t>((total_ + i) % ring_.size())] = bins[i];
    total_ += count;
}

std::uint64_t PeakHistory::Oldest() const
{
    return total_ > ring_.size() ? total_ - ring_.size() : 0;
}

PeakBin PeakHistory::Span(std::uint64_t first, std::uint64_t last) const
{
    PeakBin result;     /* silence: out-of-range bins contribute nothing */
    first = std::max(first, Oldest());
    last  = std::min(last, total_);
    for (std::uint64_t i = first; i < last; ++i) {
        const PeakBin &bin = ring_[static_cast<std::size_t>(i % ring_.size())];
        result.low  = std::min(result.low, bin.low);
        result.high = std::max(result.high, bin.high);
    }
    return result;
}

}  // namespace audiorgui
