/*
 * lane_view.h - Draws one stream's pane: title, level readout, the scrolling
 * waveform and, if the stream has one, its live transcript.
 *
 * The pane's controls (the include checkbox, the transcript switch, the
 * device drop-down, the microphone gain) are widgets placed by MainView in the
 * header area; this class draws everything else.  The waveform is a row of
 * rounded bars (see WaveformLane::Bars).  With a transcript the waveform
 * shrinks to a strip and the text, newest line at the bottom, fills the rest;
 * older lines fade towards the top.
 *
 * The view keeps its scratch buffers and the text layouts of the lines on show
 * between frames, so painting a steady-state frame allocates nothing.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../core/transcript_lane.h"
#include "../core/waveform_lane.h"
#include "painter.h"

namespace audiorgui::ui {

struct LaneStyle {
    const wchar_t *title;       /* "Microphone"            */
    D2D1_COLOR_F   color;       /* the stream's accent     */
};

/* What a pane shows of its stream's transcript.  `lane` is null when the pane
 * has none (it is switched off, or the engine cannot make one): the waveform
 * then fills the pane. */
struct TranscriptPane {
    const TranscriptLane *lane = nullptr;
    TranscriptStatus      status;
    const std::wstring   *problem = nullptr;        /* why `status` is Failed */
};

class LaneView {
public:
    /* Where the waveform starts, below the pane's controls. */
    static constexpr float kControlsHeight = 96.0f;

    /* The transcript switch sits in the header, right of the title; the
     * status of the transcript follows it. */
    static constexpr float kSwitchLeft = 172.0f, kSwitchWidth = 108.0f, kSwitchHeight = 26.0f;

    /* `included`: the stream is part of the recording (else the pane is dimmed
     * and says so).  `recording` only changes the hint shown while the lane is
     * still empty. */
    void Paint(Painter &painter, const D2D1_RECT_F &card, const LaneStyle &style,
               const WaveformLane &lane, bool included, bool recording, const TranscriptPane &transcript);

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

    void PaintBars(Painter &painter, const D2D1_RECT_F &area, const D2D1_COLOR_F &color,
                   const WaveformLane &lane);
    void PaintStatus(Painter &painter, const D2D1_RECT_F &card, const D2D1_COLOR_F &color,
                     const TranscriptPane &transcript);
    void PaintTranscript(Painter &painter, const D2D1_RECT_F &area, const D2D1_COLOR_F &color,
                         const TranscriptPane &transcript, bool recording);
    const CachedLayout &LayoutOf(Painter &painter, const TranscriptLane::Line &line, float width);

    std::vector<float> heights_;        /* reused every frame */
    long               levelKey_ = 0;   /* the dB value levelText_ shows */
    bool               levelValid_ = false;
    std::wstring       levelText_;

    long               lagKey_ = -1;    /* the lag (in tenths of a second) statusText_ shows */
    std::wstring       statusText_;

    std::uint64_t      frame_ = 0;
    CachedLayout       cache_[TranscriptLane::kMaxLines + 1];   /* one spare: a frame never evicts what it draws */
};

}  // namespace audiorgui::ui
