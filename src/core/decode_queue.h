/*
 * decode_queue.h - What the helper's decoder works on next.
 *
 * Decoding a phrase takes time, and two streams share the processor, so work
 * can arrive faster than it is done.  This queue decides what is worth doing
 * and what has been overtaken:
 *
 *   - a Partial (the phrase so far) is only interesting until a newer one for
 *     the same stream arrives, so at most one waits per stream;
 *   - a Final settles a phrase for good, so it is never replaced; and it makes
 *     the phrase's waiting Partial pointless;
 *   - Finals go first, in the order they arrived; then the older waiting Partial;
 *   - only a few Finals may wait: past that the oldest is dropped (and counted),
 *     so memory stays bounded however far behind the decoder falls.
 *
 * Not thread-safe by itself; the helper guards it.  Plain C++.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "types.h"

namespace audiorgui {

struct DecodeJob {
    StreamId                  stream = StreamId::Microphone;
    std::uint32_t             id = 0;               /* the utterance */
    std::uint64_t             startSample = 0;      /* of audio[0], in the stream */
    bool                      isFinal = false;
    double                    receivedAt = 0.0;     /* when the last of the audio arrived (monotonic seconds):
                                                       the consumer measures how far behind it is from this */
    std::vector<std::int16_t> audio;
};

class DecodeQueue {
public:
    static constexpr std::size_t kMaxFinals = 4;

    void Push(DecodeJob &&job);

    /* Takes the next job to do; false if there is none. */
    bool Pop(DecodeJob *job);

    bool        Empty() const;
    std::size_t Dropped() const { return dropped_; }    /* Finals lost to a decoder that fell too far behind */

private:
    struct Waiting {
        DecodeJob     job;
        std::uint64_t order;                            /* arrival number */
    };

    std::deque<Waiting>           finals_;
    std::optional<Waiting>        partial_[kStreamCount];
    std::uint64_t                 arrivals_ = 0;
    std::size_t                   dropped_ = 0;
};

}  // namespace audiorgui
