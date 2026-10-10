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

}  // namespace

void LaneView::Paint(Painter &p, const D2D1_RECT_F &card, const LaneStyle &style,
                     const std::wstring &device, const WaveformLane &lane, bool recording)
{
    p.FillRound(card, theme::kCardRadius, theme::kSurface);
    p.StrokeRound(card, theme::kCardRadius, theme::kBorder);

    /* Title, device name. */
    const float x = card.left + kPad;
    p.FillCircle(x + 5, card.top + 24, 5.0f, style.color);
    p.Text(style.title, RectF(x + 20, card.top + 12, card.right - 150, card.top + 36), Font::Heading,
           theme::kText);
    p.Text(device, RectF(x + 20, card.top + 34, card.right - 150, card.top + 54), Font::Small,
           theme::kTextMuted);

    /* Level readout and clip flag, right-aligned. */
    const bool clipping = lane.Clipping();
    if (recording || lane.HasAudio()) {
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

    /* The waveform. */
    const D2D1_RECT_F area = RectF(card.left + kPad, card.top + 64, card.right - kPad, card.bottom - kPad);
    if (Height(area) < 20.0f || Width(area) < 40.0f)
        return;
    PaintBars(p, area, style.color, lane);
    if (!recording && !lane.HasAudio())
        p.Text(L"Press Start to record", area, Font::Small, theme::kTextFaint, Align::Center);
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

void LaneView::PaintOff(Painter &p, const D2D1_RECT_F &card, const LaneStyle &style, const wchar_t *hint)
{
    p.FillRound(card, theme::kCardRadius, WithAlpha(theme::kSurface, 0.55f));
    p.StrokeRound(card, theme::kCardRadius, WithAlpha(theme::kBorder, 0.8f));

    const float x = card.left + kPad, cy = (card.top + card.bottom) * 0.5f;
    p.FillCircle(x + 5, cy, 5.0f, WithAlpha(style.color, 0.35f));
    p.Text(style.title, RectF(x + 20, cy - 20, x + 220, cy), Font::Heading, theme::kTextFaint);
    p.Text(hint, RectF(x + 20, cy, card.right - kPad, cy + 20), Font::Small, theme::kTextFaint);
}

}  // namespace audiorgui::ui
