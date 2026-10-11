/*
 * lag_governor.h - When is the transcript "catching up", and when has it gone
 * on so long that a smaller model should take over?
 *
 * The lag of a stream's text is how long a piece of speech waited before its
 * words were ready.  A few tenths of a second is normal; when the decoder
 * cannot keep up it grows.  Two small rules, both with a gap between the point
 * of giving in and the point of recovering so that neither flickers:
 *
 *   LagGovernor     in the helper: the stream is CatchingUp from a lag of 3 s
 *                   until it is back under 1.5 s.
 *   StepDownPolicy  in the recorder: a lag of 6 s or more, every time it is
 *                   measured for 10 s, means this model is too slow for this PC.
 *
 * Plain C++.
 */
#pragma once

#include "transcript.h"

namespace audiorgui {

class LagGovernor {
public:
    static constexpr float kCatchUpLag = 3.0f, kRecoveredLag = 1.5f;

    /* The lag of a piece of work just finished; returns the state to report. */
    TranscriptState Observe(float lagSeconds)
    {
        if (lagSeconds >= kCatchUpLag)
            catchingUp_ = true;
        else if (lagSeconds < kRecoveredLag)
            catchingUp_ = false;
        return catchingUp_ ? TranscriptState::CatchingUp : TranscriptState::Live;
    }

private:
    bool catchingUp_ = false;
};

class StepDownPolicy {
public:
    static constexpr float  kHopelessLag = 6.0f;
    static constexpr double kHoldSeconds = 10.0;

    /* (The hold time is a parameter so that the tests need not wait for it.) */
    explicit StepDownPolicy(double holdSeconds = kHoldSeconds) : hold_(holdSeconds) {}

    /* A lag measured at `nowSeconds` (any steady clock); true when it has stayed
     * hopeless for long enough that the model should be changed.  The count starts
     * again after that. */
    bool Observe(float lagSeconds, double nowSeconds)
    {
        if (lagSeconds < kHopelessLag) {
            since_ = -1.0;
            return false;
        }
        if (since_ < 0.0)
            since_ = nowSeconds;
        if (nowSeconds - since_ < hold_)
            return false;
        since_ = -1.0;
        return true;
    }

    void Reset() { since_ = -1.0; }

private:
    double hold_;
    double since_ = -1.0;                   /* when the lag first reached kHopelessLag; < 0: not now */
};

}  // namespace audiorgui
