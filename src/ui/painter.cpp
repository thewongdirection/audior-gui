#include "painter.h"

namespace audiorgui::ui {

HRESULT Painter::Bind(ID2D1RenderTarget *target)
{
    target_ = target;
    return target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_);
}

void Painter::Unbind()
{
    brush_.Reset();
    target_ = nullptr;
}

void Painter::Clear(const D2D1_COLOR_F &color)
{
    target_->Clear(color);
}

void Painter::Fill(const D2D1_RECT_F &r, const D2D1_COLOR_F &color)
{
    brush_->SetColor(color);
    target_->FillRectangle(r, brush_.Get());
}

void Painter::FillRound(const D2D1_RECT_F &r, float radius, const D2D1_COLOR_F &color)
{
    brush_->SetColor(color);
    target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
}

void Painter::StrokeRound(const D2D1_RECT_F &r, float radius, const D2D1_COLOR_F &color, float width)
{
    /* Inset by half the stroke so the line sits inside `r`. */
    const D2D1_RECT_F inner = Inflate(r, -width * 0.5f);
    brush_->SetColor(color);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(inner, radius, radius), brush_.Get(), width);
}

void Painter::FillCircle(float cx, float cy, float radius, const D2D1_COLOR_F &color)
{
    brush_->SetColor(color);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush_.Get());
}

void Painter::Line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F &color, float width)
{
    brush_->SetColor(color);
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_.Get(), width);
}

void Painter::Text(const wchar_t *text, UINT32 length, const D2D1_RECT_F &r, Font font,
                   const D2D1_COLOR_F &color, Align align)
{
    if (length == 0)
        return;
    IDWriteTextFormat *format = gfx_.Format(font);
    format->SetTextAlignment(align == Align::Left     ? DWRITE_TEXT_ALIGNMENT_LEADING
                             : align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                      : DWRITE_TEXT_ALIGNMENT_TRAILING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    brush_->SetColor(color);
    target_->DrawText(text, length, format, r, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Painter::PushClip(const D2D1_RECT_F &r)
{
    target_->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
}

void Painter::PopClip()
{
    target_->PopAxisAlignedClip();
}

}  // namespace audiorgui::ui
