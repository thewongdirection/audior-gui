/*
 * transcript_lane.h - The model behind one stream's scrolling transcript.
 *
 * Keeps the newest lines in a fixed ring (kMaxLines), so memory does not grow
 * with the length of the recording; whatever scrolls out of the ring is gone
 * from the window.  The lane applies events (see transcript.h) and animates
 * the slide of the text when a line is added, the way WaveformLane glides.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "transcript.h"

namespace audiorgui {

class TranscriptLane {
public:
    static constexpr std::size_t kMaxLines = 12;

    struct Line {
        std::uint32_t utterance    = 0;
        std::uint32_t version      = 0;     /* unique in the lane's life; changes with the text */
        bool          isFinal      = false;
        double        startSeconds = 0.0;
        std::size_t   length       = 0;
        wchar_t       text[kTranscriptChars] = {};
    };

    /* Forget everything: a new recording starts from an empty page. */
    void Reset();

    /* An event from the engine.  A new utterance adds a line (dropping the
     * oldest when the ring is full); a partial replaces its line's text; a
     * final fixes it, and later events for that utterance are ignored, as are
     * events for an utterance older than the newest line that is no longer
     * kept.  Empty text adds nothing, and an empty final takes back the live
     * line (or just closes it, if newer lines have followed). */
    void Apply(const TranscriptEvent &event);

    /* The recording is over: whatever is still being spoken is final. */
    void Settle();

    /* Let `dtSeconds` of real time pass. */
    void Advance(double dtSeconds);

    std::size_t Count() const { return count_; }
    const Line &At(std::size_t index) const { return lines_[(first_ + index) % kMaxLines]; }    /* 0 = oldest */

    /* How much of the newest line's height the text still has to rise: 1 right
     * after a line was added, down to 0.  The view slides the block by it. */
    double Slide() const { return slide_; }
    bool   Active() const { return slide_ > 0.0; }

private:
    Line *Find(std::uint32_t utterance);
    Line &Append();

    Line          lines_[kMaxLines];
    std::size_t   first_ = 0, count_ = 0;
    std::uint32_t nextVersion_ = 0;         /* not reset: a version never repeats */
    double        slide_ = 0.0;
};

}  // namespace audiorgui
