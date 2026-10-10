/*
 * lane_view.h - Draws one stream's card: title, level readout and the
 * scrolling waveform.
 *
 * The waveform is a row of rounded bars (see WaveformLane::Bars).  The view
 * keeps its scratch buffers between frames, so painting a steady-state frame
 * allocates nothing.
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
    /* `device` is the device's name, shown under the title.  `recording`
     * only changes the hint shown while the lane is still empty. */
    void Paint(Painter &painter, const D2D1_RECT_F &card, const LaneStyle &style,
               const std::wstring &device, const WaveformLane &lane, bool recording);

    /* The compact card shown for a stream that is switched off. */
    static void PaintOff(Painter &painter, const D2D1_RECT_F &card, const LaneStyle &style,
                         const wchar_t *hint);

private:
    void PaintBars(Painter &painter, const D2D1_RECT_F &area, const D2D1_COLOR_F &color,
                   const WaveformLane &lane);

    std::vector<float> heights_;        /* reused every frame */
    long               levelKey_ = 0;   /* the dB value levelText_ shows */
    bool               levelValid_ = false;
    std::wstring       levelText_;
};

}  // namespace audiorgui::ui
