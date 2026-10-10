/*
 * peak_history.h - A fixed-size history of the most recent peak bins.
 *
 * Bins are numbered from 0 upwards since the last Clear(); the history keeps
 * only the newest `capacity` of them.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "types.h"

namespace audiorgui {

class PeakHistory {
public:
    explicit PeakHistory(std::size_t capacityBins);

    void Clear();
    void Append(const PeakBin *bins, std::size_t count);

    /* Bins appended since Clear(); the next bin gets this index. */
    std::uint64_t Total() const { return total_; }

    /* The lowest bin index still held. */
    std::uint64_t Oldest() const;

    /* The extremes over bins [first, last).  Indices the history does not
     * hold (not yet received, or already dropped) count as silence. */
    PeakBin Span(std::uint64_t first, std::uint64_t last) const;

private:
    std::vector<PeakBin> ring_;
    std::uint64_t        total_ = 0;
};

}  // namespace audiorgui
