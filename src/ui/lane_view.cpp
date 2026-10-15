#include "lane_view.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include "../core/format.h"

namespace audiorgui::ui {

namespace {

constexpr float kPad       = 20.0f;
constexpr float kMinPitch  = 5.0f;      /* distance between bar starts */
constexpr float kBarFill   = 0.58f;     /* bar width as a fraction of the pitch */

constexpr float kStripHeight = 36.0f;   /* the waveform, when a transcript takes the rest of the pane */
constexpr float kStripGap    = 12.0f;
constexpr float kMinTextHeight = 24.0f; /* below this the transcript is left out */
constexpr float kClipAndLevel = 150.0f; /* the CLIP flag and the level readout, from the pane's right edge */

}  // namespace

void LaneView::Paint(Painter &p, const D2D1_RECT_F &card, const LaneStyle &style,
                     const WaveformLane &lane, bool included, bool recording, const TranscriptPane &transcript)
{
    p.FillRound(card, theme::kCardRadius, included ? theme::kSurface : WithAlpha(theme::kSurface, 0.55f));
    p.StrokeRound(card, theme::kCardRadius, included ? theme::kBorder : WithAlpha(theme::kBorder, 0.8f));

    /* The title sits right of the pane's checkbox. */
    p.Text(style.title, RectF(card.left + 52, card.top + 10, card.right - 150, card.top + 38), Font::Heading,
           included ? theme::kText : theme::kTextFaint);

    /* Level readout and clip flag, right-aligned. */
    const bool clipping = included && lane.Clipping();
    if (included && (recording || lane.HasAudio())) {
        const float db = lane.LevelDb();
        const long key = std::isfinite(db) ? std::lround(db) : LONG_MIN;
        if (!levelValid_ || key != levelKey_) {
            levelText_ = FormatDb(db);
            levelKey_ = key;
            levelValid_ = true;
        }
        p.Text(levelText_, RectF(card.right - kPad - 70, card.top + 12, card.right - kPad, card.top + 36),
               Font::Body, clipping ? theme::kError : theme::kTextMuted, Align::Right);
    }
    if (clipping) {
        const D2D1_RECT_F chip = RectXYWH(card.right - kPad - 70 - 52, card.top + 15, 44, 18);
        p.FillRound(chip, 9.0f, WithAlpha(theme::kError, 0.18f));
        p.Text(L"CLIP", chip, Font::Label, theme::kError, Align::Center);
    }

    const bool withText = included && transcript.lane != nullptr;
    if (withText)
        text_.PaintStatus(p, TranscriptView::StatusArea(card, kClipAndLevel), style.color, transcript);

    /* The waveform: the whole pane below the controls, or a strip above the transcript. */
    const D2D1_RECT_F area = RectF(card.left + kPad, card.top + kControlsHeight, card.right - kPad,
                                   card.bottom - kPad);
    if (Height(area) < 20.0f || Width(area) < 40.0f)
        return;
    if (!included) {
        p.Text(L"Not recorded", area, Font::Small, theme::kTextFaint, Align::Center);
        return;
    }

    D2D1_RECT_F wave = area, text = {};
    const bool showText = withText && Height(area) >= kStripHeight + kStripGap + kMinTextHeight;
    if (showText) {
        wave.bottom = area.top + kStripHeight;
        text = RectF(area.left, wave.bottom + kStripGap, area.right, area.bottom);
    }
    PaintBars(p, wave, style.color, lane);
    if (showText) {
        text_.PaintText(p, text, style.color, transcript, recording);
    } else if (!recording && !lane.HasAudio()) {      /* below the baseline, not on it */
        const float cy = (area.top + area.bottom) * 0.5f;
        p.Text(L"Press Start to record", RectF(area.left, cy + 12, area.right, cy + 36), Font::Small,
               theme::kTextFaint, Align::Center);
    }
}

void LaneView::PaintBars(Painter &p, const D2D1_RECT_F &area, const D2D1_COLOR_F &color,
                         const WaveformLane &lane)
{
    const float width = Width(area), height = Height(area);
    const std::size_t maxBars = lane.MaxBars();
    const float pitch = std::max(kMinPitch, width / static_cast<float>(maxBars));
    const float barW = pitch * kBarFill;
    /* One extra bar covers the part still sliding in from the right. */
    const std::size_t count = std::min(maxBars, static_cast<std::size_t>(std::ceil(width / pitch)) + 1);

    const double phase = lane.Bars(count, &heights_);
    const float newestLeft = area.right - static_cast<float>(phase) * pitch;
    const float cy = (area.top + area.bottom) * 0.5f;

    p.PushClip(area);
    p.Line(area.left, cy, area.right, cy, WithAlpha(theme::kBorder, 0.7f), 1.0f);
    for (std::size_t i = 0; i < count; ++i) {
        const float left = newestLeft - static_cast<float>(count - 1 - i) * pitch;
        if (left + barW < area.left)
            continue;
        const float h = std::max(2.0f, heights_[i] * (height - 4.0f));
        /* Older audio fades towards the left; silence is drawn fainter still. */
        const float age = std::max(0.0f, std::min((left - area.left) / width, 1.0f));
        const float alpha = (0.2f + 0.8f * age) * (heights_[i] > 0.0f ? 1.0f : 0.45f);
        p.FillRound(RectF(left, cy - h * 0.5f, left + barW, cy + h * 0.5f), barW * 0.5f,
                    WithAlpha(color, alpha));
    }
    p.PopClip();
}

}  // namespace audiorgui::ui
