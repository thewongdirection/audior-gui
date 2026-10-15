/*
 * speech_tap.h - Gives a stream's audio to the transcriber.
 *
 * Speech recognisers want 16 kHz mono; the recording is 48 kHz stereo.  A
 * capture thread runs the audio it has just converted (after the gain, like the
 * waveform sees it, and before any merging) through a SpeechDecimator and hands
 * the result to an ISpeechSink; so does the writer thread with the mix of a merge,
 * which is what that file holds.  Plain C++, tested without a device.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../core/types.h"

namespace audiorgui {

/* Where a stream's 16 kHz mono audio goes. */
class ISpeechSink {
public:
    virtual ~ISpeechSink() = default;

    /* Called on a capture thread (or, for the mix of a merge, the writer thread) with
     * the next `count` samples of the transcript's slot `stream` (see kMixTranscript).
     * It must neither block nor fail: a transcriber that cannot keep up loses
     * audio, the recording does not. */
    virtual void Push(StreamId stream, const std::int16_t *samples, std::size_t count) = 0;
};

/* 48 kHz stereo to 16 kHz mono: the channels are averaged, low-passed at about
 * 7 kHz (so nothing above 8 kHz folds back into the speech band) and every
 * third sample is kept.  State carries over between calls, so the audio may
 * come in pieces of any size. */
class SpeechDecimator {
public:
    static constexpr int kTaps = 64;                    /* the filter's length */

    /* Takes `frames` interleaved stereo frames and appends the samples they
     * complete to `out` (one per three frames, counting those of earlier calls). */
    void Add(const std::int16_t *stereo, std::size_t frames, std::vector<std::int16_t> *out);

private:
    float history_[2 * kTaps] = {};         /* the last kTaps samples, stored twice so they are contiguous */
    int   pos_ = 0;                         /* where the next sample goes (0..kTaps-1)                     */
    int   phase_ = 0;                       /* input samples since the last output                         */
};

}  // namespace audiorgui
