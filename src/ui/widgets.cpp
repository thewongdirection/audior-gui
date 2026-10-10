#include "widgets.h"

#include <algorithm>
#include <cmath>

namespace audiorgui::ui {

namespace {

constexpr float kThumbRadius = 8.0f;

/* The colour at 40% strength when the widget is disabled. */
D2D1_COLOR_F Dim(const D2D1_COLOR_F &c, bool enabled)
{
    return enabled ? c : WithAlpha(c, c.a * 0.4f);
}

}  // namespace

/* ---- Button ---------------------------------------------------------- */

void Button::Paint(Painter &p, const WidgetState &st) const
{
    D2D1_COLOR_F fill, ink = theme::kText;
    bool outlined = true;
    switch (style) {
    case Style::Primary:
        fill = st.pressed ? Rgb(0xD92D4B) : st.hot ? theme::kRecordHot : theme::kRecord;
        ink = Rgb(0xFFFFFF);
        outlined = false;
        break;
    case Style::Prominent:
        fill = st.pressed ? Rgb(0xC9CED8) : st.hot ? Rgb(0xFFFFFF) : Rgb(0xE8EBF1);
        ink = theme::kBackground;
        outlined = false;
        break;
    default:    /* Secondary, Field */
        fill = st.pressed ? theme::kBorder : st.hot ? theme::kHover : theme::kRaised;
        break;
    }

    p.FillRound(rect, theme::kControlRadius, Dim(fill, enabled));
    if (outlined)
        p.StrokeRound(rect, theme::kControlRadius, Dim(theme::kBorder, enabled));

    const float cy = (rect.top + rect.bottom) * 0.5f;
    if (style == Style::Field) {
        p.Text(text, RectF(rect.left + 12, rect.top, rect.right - 34, rect.bottom), Font::Body,
               Dim(theme::kText, enabled));
        const float cx = rect.right - 18;                       /* chevron */
        const D2D1_COLOR_F c = Dim(theme::kTextMuted, enabled);
        p.Line(cx - 4, cy - 2, cx, cy + 2, c, 1.5f);
        p.Line(cx, cy + 2, cx + 4, cy - 2, c, 1.5f);
        return;
    }

    float textLeft = rect.left;
    if (glyph == Glyph::Record) {
        p.FillCircle(rect.left + 26, cy, 6.0f, Dim(ink, enabled));
        textLeft = rect.left + 20;
    } else if (glyph == Glyph::Stop) {
        p.FillRound(RectXYWH(rect.left + 20, cy - 6, 12, 12), 2.5f, Dim(ink, enabled));
        textLeft = rect.left + 20;
    }
    p.Text(text, RectF(textLeft, rect.top, rect.right, rect.bottom), Font::Button,
           Dim(ink, enabled), Align::Center);
}

void Button::Release(float, float, bool inside)
{
    if (inside && onClick)
        onClick();
}

bool Button::Key(UINT vk)
{
    if (vk != VK_RETURN && vk != VK_SPACE)
        return false;
    if (onClick)
        onClick();
    return true;
}

/* ---- Segmented ------------------------------------------------------- */

void Segmented::Paint(Painter &p, const WidgetState &) const
{
    p.FillRound(rect, theme::kControlRadius, Dim(theme::kRaised, enabled));
    p.StrokeRound(rect, theme::kControlRadius, Dim(theme::kBorder, enabled));
    if (options.empty())
        return;

    const float segW = Width(rect) / static_cast<float>(options.size());
    for (std::size_t i = 0; i < options.size(); ++i) {
        const D2D1_RECT_F seg = RectXYWH(rect.left + segW * static_cast<float>(i), rect.top, segW, Height(rect));
        const bool isSelected = static_cast<int>(i) == selected;
        if (isSelected)
            p.FillRound(Inflate(seg, -3.0f), theme::kControlRadius - 2.0f, Dim(Rgb(0x343C4B), enabled));
        p.Text(options[i], seg, Font::BodyStrong,
               Dim(isSelected ? theme::kText : theme::kTextMuted, enabled), Align::Center);
    }
}

void Segmented::Select(int index)
{
    index = std::max(0, std::min(index, static_cast<int>(options.size()) - 1));
    if (index == selected)
        return;
    selected = index;
    if (onChange)
        onChange(index);
}

void Segmented::Press(float x, float)
{
    if (options.empty())
        return;
    const float segW = Width(rect) / static_cast<float>(options.size());
    Select(static_cast<int>((x - rect.left) / segW));
}

bool Segmented::Key(UINT vk)
{
    if (vk != VK_LEFT && vk != VK_RIGHT)
        return false;
    Select(selected + (vk == VK_RIGHT ? 1 : -1));
    return true;
}

/* ---- Toggle ---------------------------------------------------------- */

void Toggle::Paint(Painter &p, const WidgetState &st) const
{
    const float h = Height(rect), r = h * 0.5f;
    const D2D1_COLOR_F track = on ? accent : (st.hot ? theme::kBorder : Rgb(0x343C4B));
    p.FillRound(rect, r, Dim(track, enabled));
    const float cx = on ? rect.right - r : rect.left + r;
    p.FillCircle(cx, (rect.top + rect.bottom) * 0.5f, r - 3.0f, Dim(Rgb(0xFFFFFF), enabled));
}

void Toggle::Flip()
{
    on = !on;
    if (onChange)
        onChange(on);
}

void Toggle::Release(float, float, bool inside)
{
    if (inside)
        Flip();
}

bool Toggle::Key(UINT vk)
{
    if (vk != VK_RETURN && vk != VK_SPACE)
        return false;
    Flip();
    return true;
}

/* ---- Slider ---------------------------------------------------------- */

void Slider::Paint(Painter &p, const WidgetState &st) const
{
    const float left = rect.left + kThumbRadius, right = rect.right - kThumbRadius;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    const float t = maximum > minimum ? static_cast<float>(value - minimum) / static_cast<float>(maximum - minimum) : 0.0f;
    const float x = left + (right - left) * t;

    p.FillRound(RectF(left, cy - 2, right, cy + 2), 2.0f, Dim(Rgb(0x343C4B), enabled));
    p.FillRound(RectF(left, cy - 2, x, cy + 2), 2.0f, Dim(accent, enabled));
    p.FillCircle(x, cy, st.hot || st.pressed ? kThumbRadius : kThumbRadius - 1.0f, Dim(Rgb(0xFFFFFF), enabled));
}

void Slider::SetValue(int v)
{
    v = std::max(minimum, std::min(v, maximum));
    if (v == value)
        return;
    value = v;
    if (onChange)
        onChange(value);
}

void Slider::SetFromX(float x)
{
    const float left = rect.left + kThumbRadius, right = rect.right - kThumbRadius;
    const float t = std::max(0.0f, std::min((x - left) / (right - left), 1.0f));
    const int raw = minimum + static_cast<int>(std::lround(t * static_cast<float>(maximum - minimum)));
    const int s = std::max(step, 1);
    SetValue(minimum + ((raw - minimum + s / 2) / s) * s);          /* snap to the step */
}

void Slider::Press(float x, float) { SetFromX(x); }
void Slider::Drag(float x, float) { SetFromX(x); }

bool Slider::Key(UINT vk)
{
    switch (vk) {
    case VK_LEFT:  SetValue(value - step); return true;
    case VK_RIGHT: SetValue(value + step); return true;
    case VK_HOME:  SetValue(minimum);      return true;
    case VK_END:   SetValue(maximum);      return true;
    default:       return false;
    }
}

}  // namespace audiorgui::ui
