/*
 * silence_filler.h - Keeps a loopback stream as long as the recording.
 *
 * A playback device delivers no audio at all while nothing is playing, unlike
 * a microphone, which always delivers (silent) packets.  Left alone, the
 * loopback track would come out shorter than the microphone's, drifting out of
 * step in a merged file and ending up empty in a separate one.  So the capture
 * keeps count of the audio it has delivered and, whenever it is behind the
 * clock, adds the missing time as silence.
 */
#pragma once

#include <cstddef>

#include "pcm.h"

namespace audiorgui {

class SilenceFiller {
public:
    /* Called after each poll with the time since the stream started and the
     * frames that poll delivered.  Returns how many frames of silence to
     * append after them so the stream catches up with the clock; zero while it
     * is less than kMinGapFrames behind (packets arrive in bursts, and a small
     * lag is normal).  A gap longer than kMaxGapFrames is not silence but a
     * clock jump (the PC slept, the thread was starved): the stream is taken to
     * be caught up rather than padded with minutes of zeros. */
    std::size_t Owed(double elapsedSeconds, std::size_t deliveredFrames)
    {
        delivered_ += static_cast<double>(deliveredFrames);
        const double behind = elapsedSeconds * kPcmRate - delivered_;
        if (behind < static_cast<double>(kMinGapFrames))
            return 0;
        if (behind > static_cast<double>(kMaxGapFrames)) {
            delivered_ += behind;
            return 0;
        }
        const std::size_t owed = static_cast<std::size_t>(behind);
        delivered_ += static_cast<double>(owed);
        return owed;
    }

    static constexpr std::size_t kMinGapFrames = kPcmRate / 20;       /* 50 ms */
    static constexpr std::size_t kMaxGapFrames = 2 * kPcmRate;        /* 2 s   */

private:
    double delivered_ = 0.0;
};

}  // namespace audiorgui
