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

/* ---- Checkbox -------------------------------------------------------- */

void Checkbox::Paint(Painter &p, const WidgetState &st) const
{
    const float cx = (rect.left + rect.right) * 0.5f, cy = (rect.top + rect.bottom) * 0.5f;
    const D2D1_RECT_F box = RectXYWH(cx - 10.0f, cy - 10.0f, 20.0f, 20.0f);
    if (checked) {
        p.FillRound(box, 6.0f, Dim(accent, enabled));
        const D2D1_COLOR_F tick = Dim(theme::kBackground, enabled);        /* a dark tick on the accent */
        p.Line(cx - 5.0f, cy, cx - 1.5f, cy + 3.5f, tick, 2.2f);
        p.Line(cx - 1.5f, cy + 3.5f, cx + 5.0f, cy - 3.5f, tick, 2.2f);
    } else {
        p.FillRound(box, 6.0f, Dim(st.hot ? theme::kHover : theme::kRaised, enabled));
        p.StrokeRound(box, 6.0f, Dim(theme::kTextFaint, enabled), 1.5f);
    }
}

void Checkbox::Flip()
{
    checked = !checked;
    if (onChange)
        onChange(checked);
}

void Checkbox::Release(float, float, bool inside)
{
    if (inside)
        Flip();
}

bool Checkbox::Key(UINT vk)
{
    if (vk != VK_RETURN && vk != VK_SPACE)
        return false;
    Flip();
    return true;
}

/* ---- Switch ---------------------------------------------------------- */

void Switch::Paint(Painter &p, const WidgetState &st) const
{
    const float radius = Height(rect) * 0.5f, cy = (rect.top + rect.bottom) * 0.5f;
    const D2D1_COLOR_F fill = checked ? WithAlpha(accent, 0.16f) : st.hot ? theme::kHover : theme::kRaised;
    p.FillRound(rect, radius, Dim(fill, enabled));
    p.StrokeRound(rect, radius, Dim(checked ? WithAlpha(accent, 0.7f) : theme::kBorder, enabled));

    const D2D1_RECT_F track = RectXYWH(rect.left + 9.0f, cy - 6.0f, 22.0f, 12.0f);
    p.FillRound(track, 6.0f, Dim(checked ? accent : Rgb(0x343C4B), enabled));
    p.FillCircle(checked ? track.right - 6.0f : track.left + 6.0f, cy, 4.0f,
                 Dim(checked ? theme::kBackground : theme::kTextMuted, enabled));
    p.Text(text, RectF(track.right + 8.0f, rect.top, rect.right - 10.0f, rect.bottom), Font::Small,
           Dim(checked ? theme::kText : theme::kTextMuted, enabled));
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

/* ---- TextBox --------------------------------------------------------- */

namespace {

constexpr float kTextPad = 12.0f;               /* between the box's edge and its text */

bool Down(int virtualKey) { return GetKeyState(virtualKey) < 0; }

}  // namespace

void TextBox::SetText(const std::wstring &text)
{
    if (editor.Text() != text)
        editor.SetText(text);
}

void TextBox::Edited(bool changed)
{
    if (changed && onChange)
        onChange(editor.Text());
}

/* The layout of the current text, made when the text changed. */
void TextBox::EnsureLayout(Painter &painter) const
{
    if (layout_ && layoutText_ == editor.Text())
        return;
    layoutText_ = editor.Text();
    layout_ = layoutText_.empty() ? ComPtr<IDWriteTextLayout>() : painter.Layout(Font::Body, layoutText_);
}

float TextBox::XOf(std::size_t position) const
{
    const std::size_t length = layoutText_.size();
    if (!layout_ || position == 0 || length == 0)
        return 0.0f;
    float x = 0.0f, y = 0.0f;
    DWRITE_HIT_TEST_METRICS metrics;
    if (position >= length)                     /* after the last character: its trailing edge */
        layout_->HitTestTextPosition(static_cast<UINT32>(length - 1), TRUE, &x, &y, &metrics);
    else
        layout_->HitTestTextPosition(static_cast<UINT32>(position), FALSE, &x, &y, &metrics);
    return x;
}

/* The caret position nearest to `x` (in DIPs, window coordinates). */
std::size_t TextBox::PositionAt(float x) const
{
    if (!layout_)
        return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics;
    layout_->HitTestPoint(x - (rect.left + kTextPad) + scroll_, 1.0f, &trailing, &inside, &metrics);
    return metrics.textPosition + (trailing ? metrics.length : 0);
}

void TextBox::Paint(Painter &p, const WidgetState &st) const
{
    EnsureLayout(p);
    const D2D1_COLOR_F border = invalid ? theme::kError : st.active ? theme::kFocusRing : theme::kBorder;
    p.FillRound(rect, theme::kControlRadius, Dim(st.hot && !st.active ? theme::kHover : theme::kRaised, enabled));
    p.StrokeRound(rect, theme::kControlRadius, Dim(border, enabled), st.active ? 1.5f : 1.0f);

    const D2D1_RECT_F inner = RectF(rect.left + kTextPad, rect.top, rect.right - kTextPad, rect.bottom);
    const std::wstring &text = editor.Text();
    const D2D1_COLOR_F ink = Dim(theme::kText, enabled);
    if (text.empty()) {
        p.Text(placeholder, inner, Font::Body, Dim(theme::kTextFaint, enabled));
    } else if (!layout_) {
        p.Text(text, inner, Font::Body, ink);                   /* no layout to measure: no caret or scrolling */
    } else {
        DWRITE_TEXT_METRICS m = {};
        layout_->GetMetrics(&m);
        const float width = m.widthIncludingTrailingWhitespace;
        const float caretX = XOf(editor.Caret());
        const float room = Width(inner);
        if (width <= room)
            scroll_ = 0.0f;
        else if (caretX - scroll_ > room - 2.0f)                /* keep the caret in view */
            scroll_ = caretX - room + 2.0f;
        else if (caretX < scroll_)
            scroll_ = caretX;
        scroll_ = std::max(0.0f, std::min(scroll_, width - room + 2.0f));      /* no blank space after shortening */

        p.PushClip(inner);
        if (st.active && editor.HasSelection()) {
            p.Fill(RectF(inner.left + XOf(editor.SelectionStart()) - scroll_, rect.top + 8,
                         inner.left + XOf(editor.SelectionEnd()) - scroll_, rect.bottom - 8),
                   WithAlpha(theme::kFocusRing, 0.35f));
        }
        const float left = inner.left - scroll_;
        p.Text(text, RectF(left, inner.top, left + width + 4.0f, inner.bottom), Font::Body, ink);
        p.PopClip();
    }

    if (st.active && enabled) {
        const float x = inner.left + XOf(editor.Caret()) - scroll_;
        p.Line(x, rect.top + 9, x, rect.bottom - 9, theme::kText, 1.5f);
    }
}

void TextBox::Press(float x, float)
{
    editor.Move(PositionAt(x), Down(VK_SHIFT));
}

void TextBox::Drag(float x, float)
{
    editor.Move(PositionAt(x), true);
}

bool TextBox::Key(UINT vk)
{
    const bool shift = Down(VK_SHIFT), ctrl = Down(VK_CONTROL);
    switch (vk) {
    case VK_LEFT:   editor.Left(shift, ctrl);  return true;
    case VK_RIGHT:  editor.Right(shift, ctrl); return true;
    case VK_HOME:   editor.Home(shift);        return true;
    case VK_END:    editor.End(shift);         return true;
    case VK_BACK:   Edited(editor.Backspace(ctrl)); return true;
    case VK_DELETE: Edited(editor.Delete(ctrl));    return true;
    default:        break;
    }
    if (!ctrl || Down(VK_MENU))                 /* AltGr is Ctrl+Alt and types characters: leave those to WM_CHAR */
        return false;
    switch (vk) {
    case 'A':
        editor.SelectAll();
        return true;
    case 'C':
    case 'X':
        if (editor.HasSelection() && clipboardSet)
            clipboardSet(editor.Selected());
        if (vk == 'X')
            Edited(editor.DeleteSelection());
        return true;
    case 'V':
        if (clipboardGet)
            Edited(editor.Insert(clipboardGet()));
        return true;
    default:
        return false;
    }
}

bool TextBox::Char(wchar_t c)
{
    if (c < 32 || c == 0x7F)                    /* Enter, Tab, Backspace and the Ctrl+letters are keys, not text */
        return false;
    Edited(editor.Insert(std::wstring(1, c)));
    return true;
}

}  // namespace audiorgui::ui
