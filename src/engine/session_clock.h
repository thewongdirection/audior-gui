/*
 * session_clock.h - The time origin the streams of one recording share.
 *
 * The streams start independently, and devices differ enormously in how long that takes: a
 * loopback in a few milliseconds, a Bluetooth headset's microphone in most of a second.  If each
 * stream measured time from its own start, the one that started first would run ahead of the
 * others by exactly that difference, and its track would come out longer and out of step.
 * Instead every stream claims the origin as soon as its device is running; the earliest claim
 * wins, and each stream pads itself up to the clock that origin defines (see silence_filler.h),
 * so a late starter begins with the silence it missed and the tracks line up.  A stream that
 * is alone is first, so it never gets a gap: nothing is missed that it could be in step with.
 */
#pragma once

#include <atomic>

namespace audiorgui {

class SessionClock {
public:
    /* A stream calls this with the performance-counter reading taken as its device began
     * running.  Returns the origin: the earliest reading any stream has given. */
    long long Claim(long long ticks)
    {
        long long origin = origin_.load();
        while ((origin == 0 || ticks < origin) && !origin_.compare_exchange_weak(origin, ticks)) {
        }
        return origin_.load();
    }

private:
    std::atomic<long long> origin_{0};          /* 0: nobody has started yet */
};

}  // namespace audiorgui
