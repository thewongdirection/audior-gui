/*
 * painter.h - Drawing primitives over one Direct2D render target.
 *
 * The brush and the render target are device-dependent, so a Painter is bound
 * to a target when it is created and unbound when the device is lost.  One
 * brush is reused for every fill, so painting allocates nothing.
 */
#pragma once

#include <cwchar>
#include <string>

#include "geometry.h"
#include "gfx.h"
#include "theme.h"

namespace audiorgui::ui {

enum class Align { Left, Center, Right };

class Painter {
public:
    explicit Painter(const Gfx &gfx) : gfx_(gfx) {}

    HRESULT Bind(ID2D1RenderTarget *target);
    void    Unbind();

    ID2D1RenderTarget *Target() const { return target_; }

    void Clear(const D2D1_COLOR_F &color);
    void Fill(const D2D1_RECT_F &r, const D2D1_COLOR_F &color);
    void FillRound(const D2D1_RECT_F &r, float radius, const D2D1_COLOR_F &color);
    void StrokeRound(const D2D1_RECT_F &r, float radius, const D2D1_COLOR_F &color, float width = 1.0f);
    void FillCircle(float cx, float cy, float radius, const D2D1_COLOR_F &color);
    void Line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F &color, float width = 1.0f);

    /* One line of text, vertically centred in `r`, ending in "..." if it does not fit. */
    void Text(const wchar_t *text, UINT32 length, const D2D1_RECT_F &r, Font font,
              const D2D1_COLOR_F &color, Align align = Align::Left);
    void Text(const std::wstring &text, const D2D1_RECT_F &r, Font font,
              const D2D1_COLOR_F &color, Align align = Align::Left)
    {
        Text(text.c_str(), static_cast<UINT32>(text.size()), r, font, color, align);
    }
    void Text(const wchar_t *text, const D2D1_RECT_F &r, Font font,
              const D2D1_COLOR_F &color, Align align = Align::Left)
    {
        Text(text, static_cast<UINT32>(wcslen(text)), r, font, color, align);
    }

    void PushClip(const D2D1_RECT_F &r);
    void PopClip();

private:
    const Gfx                    &gfx_;
    ID2D1RenderTarget            *target_ = nullptr;     /* not owned */
    ComPtr<ID2D1SolidColorBrush>  brush_;
};

}  // namespace audiorgui::ui
