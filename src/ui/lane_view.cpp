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
constexpr float kLineGap     = 6.0f;
constexpr float kStampWidth  = 40.0f;   /* the time a line began, left of its text */
constexpr float kStampGap    = 10.0f;
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
        PaintStatus(p, card, style.color, transcript);

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
        PaintTranscript(p, text, style.color, transcript, recording);
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

/* What the transcript is doing, in the header between its switch and the level. */
void LaneView::PaintStatus(Painter &p, const D2D1_RECT_F &card, const D2D1_COLOR_F &color,
                           const TranscriptPane &t)
{
    if (t.status.state == TranscriptState::Off)
        return;
    const D2D1_RECT_F r = RectF(card.left + kSwitchLeft + kSwitchWidth + 12.0f, card.top + 12,
                                card.right - kClipAndLevel - 8.0f, card.top + 36);
    if (Width(r) < 40.0f)
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

const LaneView::CachedLayout &LaneView::LayoutOf(Painter &p, const TranscriptLane::Line &line, float width)
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
void LaneView::PaintTranscript(Painter &p, const D2D1_RECT_F &area, const D2D1_COLOR_F &color,
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
