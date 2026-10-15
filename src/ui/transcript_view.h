/*
 * transcript_view.h - Draws a live transcript: its status and its lines.
 *
 * Two kinds of card show one: a stream's pane (LaneView), under its waveform, and the card between
 * the panes when the two streams are merged (MixView).  Both put the transcript's switch in the
 * header, right of the title, and the status after it.  The lines run from the newest at the
 * bottom up; a new line pushes the text up from below and older lines fade towards the top.
 *
 * The view keeps the text layouts of the lines on show between frames, so painting a steady-state
 * frame allocates nothing.
 */
#pragma once

#include <cstdint>
#include <string>

#include "../core/transcript_lane.h"
#include "painter.h"

namespace audiorgui::ui {

/* What a card shows of a transcript.  `lane` is null when the card has none (it is switched off, or
 * the engine cannot make one). */
struct TranscriptPane {
    const TranscriptLane *lane = nullptr;
    TranscriptStatus      status;
    const std::wstring   *problem = nullptr;        /* why `status` is Failed */
};

class TranscriptView {
public:
    /* The switch sits in the card's header, right of the title; the status of the transcript follows it. */
    static constexpr float kSwitchLeft = 172.0f, kSwitchWidth = 148.0f, kSwitchHeight = 26.0f;

    /* The status's room in a header: after the switch, up to `reserved` short of the card's right edge,
     * where the rest of the header says its own things. */
    static D2D1_RECT_F StatusArea(const D2D1_RECT_F &card, float reserved);

    /* What the transcript is doing, in `area`, in the card's accent `color`. */
    void PaintStatus(Painter &painter, const D2D1_RECT_F &area, const D2D1_COLOR_F &color, const TranscriptPane &transcript);

    /* The lines, in `area`.  `recording` only changes the hint shown while there are none. */
    void PaintText(Painter &painter, const D2D1_RECT_F &area, const D2D1_COLOR_F &color, const TranscriptPane &transcript,
                   bool recording);

private:
    /* A line's text laid out at some width: rebuilt only when the line's text
     * (its version) or the width changes. */
    struct CachedLayout {
        std::uint32_t             version = 0;      /* 0: unused */
        float                     width = 0.0f;
        std::uint64_t             used = 0;         /* the frame it was last needed in */
        ComPtr<IDWriteTextLayout> layout;
        float                     height = 0.0f, rowHeight = 0.0f;
        std::wstring              stamp;            /* when the line began: "0:12" */
    };

    const CachedLayout &LayoutOf(Painter &painter, const TranscriptLane::Line &line, float width);

    long               lagKey_ = -1;    /* the lag (in tenths of a second) statusText_ shows */
    std::wstring       statusText_;

    std::uint64_t      frame_ = 0;
    CachedLayout       cache_[TranscriptLane::kMaxLines + 1];   /* one spare: a frame never evicts what it draws */
};

}  // namespace audiorgui::ui
