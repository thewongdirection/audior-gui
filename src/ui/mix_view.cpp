#include "mix_view.h"

namespace audiorgui::ui {

namespace {

constexpr float kPad          = 20.0f;
constexpr float kCaptionRoom  = 150.0f; /* the caption, from the card's right edge: the status stops short of it */
constexpr float kMinTextHeight = 24.0f; /* below this the transcript is left out */

}  // namespace

void MixView::Paint(Painter &p, const D2D1_RECT_F &card, const TranscriptPane &transcript, bool recording)
{
    p.FillRound(card, theme::kCardRadius, theme::kSurface);
    p.StrokeRound(card, theme::kCardRadius, theme::kBorder);

    /* Where a pane has its checkbox, the two streams that are mixed, one over the other. */
    p.FillCircle(card.left + 26.0f, card.top + 24.0f, 6.0f, WithAlpha(theme::kMic, 0.9f));
    p.FillCircle(card.left + 34.0f, card.top + 24.0f, 6.0f, WithAlpha(theme::kSystem, 0.9f));
    p.Text(L"Transcript", RectF(card.left + 52, card.top + 10, card.left + TranscriptView::kSwitchLeft - 8, card.top + 38),
           Font::Heading, theme::kText);
    p.Text(L"Mix of both streams", RectF(card.right - kCaptionRoom, card.top + 12, card.right - kPad, card.top + 36),
           Font::Small, theme::kTextFaint, Align::Right);

    if (!transcript.lane)
        return;
    text_.PaintStatus(p, TranscriptView::StatusArea(card, kCaptionRoom), theme::kMix, transcript);

    const D2D1_RECT_F text = RectF(card.left + kPad, card.top + kBarHeight + 4.0f, card.right - kPad, card.bottom - 16.0f);
    if (Height(text) >= kMinTextHeight && Width(text) >= 40.0f)
        text_.PaintText(p, text, theme::kMix, transcript, recording);
}

}  // namespace audiorgui::ui
