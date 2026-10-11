/*
 * gfx.h - The process-wide graphics factories and the text formats.
 *
 * These objects do not depend on a window or a device, so they are created
 * once and shared (Direct2D, DirectWrite and WIC all ship with Windows).
 */
#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace audiorgui::ui {

using Microsoft::WRL::ComPtr;

/* The type scale.  Sizes are in DIPs; see Gfx::Init for the table. */
enum class Font { Title, Heading, Body, BodyStrong, Small, Label, Time, Button, Count };

class Gfx {
public:
    HRESULT Init();

    ID2D1Factory       *D2D() const { return d2d_.Get(); }
    IWICImagingFactory *Wic() const { return wic_.Get(); }
    IDWriteTextFormat  *Format(Font font) const { return formats_[static_cast<int>(font)].Get(); }

    /* `text` laid out on one line (no wrapping, no trimming), for measuring
     * it and finding the character under a point.  Null if it cannot be made. */
    ComPtr<IDWriteTextLayout> Layout(Font font, const wchar_t *text, UINT32 length) const;

    /* `text` wrapped at `width` DIPs, as many lines as it takes, for drawing
     * and measuring (its metrics give the height).  Null if it cannot be made. */
    ComPtr<IDWriteTextLayout> WrappedLayout(Font font, const wchar_t *text, UINT32 length, float width) const;

private:
    HRESULT MakeFormat(Font font, const wchar_t *family, float size, DWRITE_FONT_WEIGHT weight);

    ComPtr<ID2D1Factory>       d2d_;
    ComPtr<IDWriteFactory>     dwrite_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IDWriteTextFormat>  formats_[static_cast<int>(Font::Count)];
    ComPtr<IDWriteInlineObject> ellipsis_[static_cast<int>(Font::Count)];
};

}  // namespace audiorgui::ui
