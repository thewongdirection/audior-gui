#include "transcript_lane.h"

#include <algorithm>
#include <cwchar>

namespace audiorgui {

namespace {

constexpr double kSlideSeconds = 0.22;

/* The text's length, never more than a line holds (room for the terminator is
 * kept even if the event's text has none). */
std::size_t LengthOf(const wchar_t *text)
{
    std::size_t n = 0;
    while (n < kTranscriptChars - 1 && text[n] != L'\0')
        ++n;
    return n;
}

}  // namespace

void TranscriptLane::Reset()
{
    first_ = count_ = 0;
    slide_ = 0.0;
}

TranscriptLane::Line *TranscriptLane::Find(std::uint32_t utterance)
{
    for (std::size_t i = count_; i-- > 0;) {            /* the live line is the newest */
        Line &line = lines_[(first_ + i) % kMaxLines];
        if (line.utterance == utterance)
            return &line;
    }
    return nullptr;
}

TranscriptLane::Line &TranscriptLane::Append()
{
    if (count_ == kMaxLines) {
        first_ = (first_ + 1) % kMaxLines;
        --count_;
    }
    Line &line = lines_[(first_ + count_) % kMaxLines];
    ++count_;
    line = Line();
    return line;
}

void TranscriptLane::Apply(const TranscriptEvent &event)
{
    const std::size_t length = LengthOf(event.text);
    Line *line = Find(event.utterance);
    if (line && line->isFinal)
        return;

    if (length == 0) {
        if (line && event.isFinal) {
            if (line == &lines_[(first_ + count_ - 1) % kMaxLines])
                --count_;                               /* nothing was recognised after all */
            else
                line->isFinal = true;                   /* a line in the middle cannot be taken back */
        }
        return;
    }

    if (!line) {
        if (count_ > 0 && event.utterance < lines_[(first_ + count_ - 1) % kMaxLines].utterance)
            return;                                     /* late news of a line that has scrolled away */
        line = &Append();
        line->utterance = event.utterance;
        line->startSeconds = event.startSeconds;
        slide_ = 1.0;
    }
    if (line->length != length || std::wmemcmp(line->text, event.text, length) != 0) {
        std::wmemcpy(line->text, event.text, length);
        line->text[length] = L'\0';
        line->length = length;
        line->version = ++nextVersion_;
    }
    if (event.isFinal)
        line->isFinal = true;
}

void TranscriptLane::Settle()
{
    if (count_ > 0)
        lines_[(first_ + count_ - 1) % kMaxLines].isFinal = true;
}

void TranscriptLane::Advance(double dtSeconds)
{
    if (slide_ > 0.0)
        slide_ = std::max(0.0, slide_ - dtSeconds / kSlideSeconds);
}

}  // namespace audiorgui
