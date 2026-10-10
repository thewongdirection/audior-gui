/*
 * lane_view.h - Draws one stream's pane: title, level readout and the
 * scrolling waveform.
 *
 * The pane's controls (the include checkbox, the device drop-down, the
 * microphone gain) are widgets placed by MainView in the header area; this
 * class draws everything else.  The waveform is a row of rounded bars (see
 * WaveformLane::Bars).  The view keeps its scratch buffers between frames, so
 * painting a steady-state frame allocates nothing.
 */
#pragma once

#include <string>
#include <vector>

#include "../core/waveform_lane.h"
#include "painter.h"

namespace audiorgui::ui {

struct LaneStyle {
    const wchar_t *title;       /* "Microphone"            */
    D2D1_COLOR_F   color;       /* the stream's accent     */
};

class LaneView {
public:
    /* Where the waveform starts, below the pane's controls. */
    static constexpr float kControlsHeight = 96.0f;

    /* `included`: the stream is part of the recording (else the pane is dimmed
     * and says so).  `recording` only changes the hint shown while the lane is
     * still empty. */
    void Paint(Painter &painter, const D2D1_RECT_F &card, const LaneStyle &style,
               const WaveformLane &lane, bool included, bool recording);

private:
    void PaintBars(Painter &painter, const D2D1_RECT_F &area, const D2D1_COLOR_F &color,
                   const WaveformLane &lane);

    std::vector<float> heights_;        /* reused every frame */
    long               levelKey_ = 0;   /* the dB value levelText_ shows */
    bool               levelValid_ = false;
    std::wstring       levelText_;
};

}  // namespace audiorgui::ui
