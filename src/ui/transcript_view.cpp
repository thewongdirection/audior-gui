#include "transcript_view.h"

#include <algorithm>
#include <cmath>

#include "../core/format.h"

namespace audiorgui::ui {

namespace {

constexpr float kLineGap     = 6.0f;
constexpr float kStampWidth  = 40.0f;   /* the time a line began, left of its text */
constexpr float kStampGap    = 10.0f;

}  // namespace

D2D1_RECT_F TranscriptView::StatusArea(const D2D1_RECT_F &card, float reserved)
{
    return RectF(card.left + kSwitchLeft + kSwitchWidth + 12.0f, card.top + 12, card.right - reserved - 8.0f,
                 card.top + 36);
}

void TranscriptView::PaintStatus(Painter &p, const D2D1_RECT_F &r, const D2D1_COLOR_F &color, const TranscriptPane &t)
{
    if (t.status.state == TranscriptState::Off || Width(r) < 40.0f)
        return;

    switch (t.status.state) {
    case TranscriptState::Starting:
        p.Text(L"Starting…", r, Font::Small, theme::kTextMuted);
        break;
    case TranscriptState::Live:
    case TranscriptState::CatchingUp: {
        const bool catching = t.status.state == TranscriptState::CatchingUp;
        const bool compact = Width(r) < 150.0f;                 /* no room for the lag */
        const long key = (compact ? -2 : std::lround(t.status.lagSeconds * 10.0f)) * 4 + (catching ? 2 : 0) +
                         (t.status.smallerModel ? 1 : 0);
        if (key != lagKey_) {
            statusText_ = catching ? L"Catching up" : L"Live";
            if (!compact) {
                if (t.status.smallerModel)
                    statusText_ += L"  ·  smaller model";
                statusText_ += L"  ·  " + FormatLag(t.status.lagSeconds) + L" behind";
            }
            lagKey_ = key;
        }
        const D2D1_COLOR_F &shade = catching ? theme::kWarn : color;
        p.FillCircle(r.left + 4.0f, (r.top + r.bottom) * 0.5f, 3.5f, shade);
        p.Text(statusText_, RectF(r.left + 14.0f, r.top, r.right, r.bottom), Font::Small, shade);
        break;
    }
    case TranscriptState::Failed:
        p.Text(L"Transcript stopped", r, Font::Small, theme::kError);
        break;
    default:
        break;
    }
}

const TranscriptView::CachedLayout &TranscriptView::LayoutOf(Painter &p, const TranscriptLane::Line &line, float width)
{
    CachedLayout *oldest = &cache_[0];
    for (CachedLayout &c : cache_) {
        if (c.version == line.version && c.width == width) {
            c.used = frame_;
            return c;
        }
        if (c.used < oldest->used)
            oldest = &c;
    }

    CachedLayout &c = *oldest;
    c.version = line.version;
    c.width = width;
    c.used = frame_;
    c.stamp = FormatClock(line.startSeconds);
    c.height = c.rowHeight = 0.0f;
    c.layout = p.WrappedLayout(Font::Body, line.text, static_cast<UINT32>(line.length), width);
    DWRITE_TEXT_METRICS metrics;
    if (c.layout && SUCCEEDED(c.layout->GetMetrics(&metrics)) && metrics.lineCount > 0) {
        c.height = metrics.height;
        c.rowHeight = metrics.height / static_cast<float>(metrics.lineCount);
    }
    return c;
}

/* The lines from the newest at the bottom up, as many as fit.  A new line
 * pushes the text up from below; older lines fade towards the top. */
void TranscriptView::PaintText(Painter &p, const D2D1_RECT_F &area, const D2D1_COLOR_F &color,
                               const TranscriptPane &t, bool recording)
{
    const TranscriptLane &lane = *t.lane;
    if (lane.Count() == 0) {
        if (t.status.state == TranscriptState::Failed) {
            const bool known = t.problem && !t.problem->empty();
            p.Text(known ? *t.problem : std::wstring(L"The transcript stopped."), area, Font::Small,
                   theme::kError, Align::Center);
        } else
            p.Text(recording ? L"Listening…" : L"The transcript appears here while recording", area,
                   Font::Small, theme::kTextFaint, Align::Center);
        return;
    }

    ++frame_;
    const float textLeft = area.left + kStampWidth + kStampGap;
    const float textWidth = std::max(40.0f, area.right - textLeft);
    const float slide = static_cast<float>(lane.Slide());
    float bottom = area.bottom + slide * slide *
                                     (LayoutOf(p, lane.At(lane.Count() - 1), textWidth).height + kLineGap);
    const float fadeSpan = Height(area) * 0.6f;

    p.PushClip(area);
    for (std::size_t i = lane.Count(); i-- > 0 && bottom > area.top;) {
        const TranscriptLane::Line &line = lane.At(i);
        const CachedLayout &c = LayoutOf(p, line, textWidth);
        if (!c.layout)
            continue;

        const float top = bottom - c.height;
        const float age = std::min(1.0f, std::max(0.0f, (top + c.height * 0.5f - area.top) / fadeSpan));
        const float alpha = 0.25f + 0.75f * age;
        p.Draw(c.layout.Get(), textLeft, top, WithAlpha(line.isFinal ? theme::kText : theme::kTextMuted, alpha));
        if (line.isFinal) {
            p.Text(c.stamp, RectXYWH(area.left, top, kStampWidth, c.rowHeight), Font::Small,
                   WithAlpha(theme::kTextFaint, alpha));
        } else {                                        /* still being spoken: a caret after the last word */
            FLOAT x = 0.0f, y = 0.0f;
            DWRITE_HIT_TEST_METRICS at;
            if (SUCCEEDED(c.layout->HitTestTextPosition(static_cast<UINT32>(line.length), FALSE, &x, &y, &at)))
                p.Fill(RectXYWH(textLeft + x + 2.0f, top + y + 2.0f, 2.0f, std::max(at.height - 4.0f, 4.0f)),
                       WithAlpha(color, alpha));
        }
        bottom = top - kLineGap;
    }
    p.PopClip();
}

}  // namespace audiorgui::ui
