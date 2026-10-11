#include "gfx.h"

namespace audiorgui::ui {

namespace {

/* "Segoe UI Variable" ships with Windows 11; Windows 10 has "Segoe UI". */
const wchar_t *PickFamily(IDWriteFactory *dwrite)
{
    ComPtr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(dwrite->GetSystemFontCollection(&fonts, FALSE)) &&
        SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists)
        return L"Segoe UI Variable Text";
    return L"Segoe UI";
}

}  // namespace

HRESULT Gfx::MakeFormat(Font font, const wchar_t *family, float size, DWRITE_FONT_WEIGHT weight)
{
    const int i = static_cast<int>(font);
    HRESULT hr = dwrite_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, size, L"en-us",
                                           &formats_[i]);
    if (FAILED(hr))
        return hr;

    /* Single lines that end in an ellipsis rather than wrapping. */
    formats_[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(formats_[i].Get(), &ellipsis_[i]))) {
        const DWRITE_TRIMMING trimming = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        formats_[i]->SetTrimming(&trimming, ellipsis_[i].Get());
    }
    return S_OK;
}

ComPtr<IDWriteTextLayout> Gfx::Layout(Font font, const wchar_t *text, UINT32 length) const
{
    ComPtr<IDWriteTextLayout> layout;
    const float kUnbounded = 100000.0f;
    if (FAILED(dwrite_->CreateTextLayout(text, length, Format(font), kUnbounded, kUnbounded, &layout)))
        layout.Reset();
    else
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);       /* positions count from the text's left edge */
    return layout;
}

ComPtr<IDWriteTextLayout> Gfx::WrappedLayout(Font font, const wchar_t *text, UINT32 length, float width) const
{
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text, length, Format(font), width, 100000.0f, &layout))) {
        layout.Reset();
        return layout;
    }
    /* The shared format is set up for single lines, and Painter::Text bends its
     * alignment: say what is wanted here. */
    const DWRITE_TRIMMING none = {DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    layout->SetTrimming(&none, nullptr);
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    return layout;
}

HRESULT Gfx::Init()
{
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf());
    if (FAILED(hr))
        return hr;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown **>(dwrite_.GetAddressOf()));
    if (FAILED(hr))
        return hr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&wic_));
    if (FAILED(hr))
        return hr;

    const wchar_t *family = PickFamily(dwrite_.Get());
    struct Spec { Font font; float size; DWRITE_FONT_WEIGHT weight; };
    static const Spec kSpecs[] = {
        {Font::Title,      22.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {Font::Heading,    15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {Font::Body,       13.0f, DWRITE_FONT_WEIGHT_REGULAR},
        {Font::BodyStrong, 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {Font::Small,      12.0f, DWRITE_FONT_WEIGHT_REGULAR},
        {Font::Label,      11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},
        {Font::Time,       36.0f, DWRITE_FONT_WEIGHT_LIGHT},
        {Font::Button,     14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD},
    };
    for (const Spec &s : kSpecs) {
        hr = MakeFormat(s.font, family, s.size, s.weight);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

}  // namespace audiorgui::ui
