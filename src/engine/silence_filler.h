/*
 * silence_filler.h - Keeps a stream as long as the recording.
 *
 * A stream's audio has to line up with the clock, or the tracks of a recording drift apart:
 *
 *   - a playback device delivers no audio at all while nothing is playing, unlike a
 *     microphone, which always delivers (silent) packets.  Left alone, the loopback track would
 *     come out shorter than the microphone's, drifting out of step in a merged file and ending
 *     up empty in a separate one;
 *   - a device may take a while to start (a Bluetooth microphone needs most of a second to
 *     switch profile), during which the other streams are already running.
 *
 * So the capture keeps count of the audio it has delivered and, whenever it is behind the
 * clock, adds the missing time as silence, in front of the audio that has just arrived.
 * The clock is shared by all the streams of a recording (see session_clock.h).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pcm.h"

namespace audiorgui {

class SilenceFiller {
public:
    /* Called after each poll with the time since the recording began and the frames that poll
     * delivered.  Returns how many frames of silence the stream owes to catch up with the clock;
     * zero while it is less than kMinGapFrames behind (packets arrive in bursts, and a small lag
     * is normal).  A gap longer than kMaxGapFrames is not silence but a clock jump (the PC
     * slept, the thread was starved): the stream is taken to be caught up rather than padded
     * with minutes of zeros.  The first call is allowed more, up to kMaxStartGapFrames: that
     * gap is the time the device took to start. */
    std::size_t Owed(double elapsedSeconds, std::size_t deliveredFrames)
    {
        const std::size_t limit = started_ ? kMaxGapFrames : kMaxStartGapFrames;
        started_ = true;
        delivered_ += static_cast<double>(deliveredFrames);
        const double behind = elapsedSeconds * kPcmRate - delivered_;
        if (behind < static_cast<double>(kMinGapFrames))
            return 0;
        if (behind > static_cast<double>(limit)) {
            delivered_ += behind;
            return 0;
        }
        const std::size_t owed = static_cast<std::size_t>(behind);
        delivered_ += static_cast<double>(owed);
        return owed;
    }

    /* The same for a poll's audio: `frames` holds canonical frames, those of this poll starting
     * at sample index `before`.  The silence owed goes in front of them, because the time
     * that passed without audio came first. */
    void Pad(double elapsedSeconds, std::vector<std::int16_t> *frames, std::size_t before)
    {
        const std::size_t delivered = (frames->size() - before) / kPcmChannels;
        const std::size_t owed = Owed(elapsedSeconds, delivered);
        frames->insert(frames->begin() + static_cast<std::ptrdiff_t>(before), owed * kPcmChannels, std::int16_t(0));
    }

    static constexpr std::size_t kMinGapFrames      = kPcmRate / 20;        /* 50 ms */
    static constexpr std::size_t kMaxGapFrames      = 2 * kPcmRate;         /* 2 s   */
    static constexpr std::size_t kMaxStartGapFrames = 30 * kPcmRate;        /* 30 s  */

private:
    double delivered_ = 0.0;
    bool   started_ = false;
};

}  // namespace audiorgui
