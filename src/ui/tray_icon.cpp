#include "tray_icon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "theme.h"

namespace audiorgui::ui {

namespace {

constexpr UINT kTrayId = 1;

void Copy(wchar_t *to, std::size_t capacity, const std::wstring &from)
{
    wcsncpy_s(to, capacity, from.c_str(), _TRUNCATE);       /* a long text is cut, not refused */
}

/* How much of a pixel lies inside a shape, from its signed distance to the
 * edge in pixels (negative inside). */
float Coverage(float distance) { return std::max(0.0f, std::min(1.0f, 0.5f - distance)); }

/* Premultiplied colour `c` at strength `a`, drawn over what is already in `px` (r, g, b, alpha). */
void Over(float px[4], const D2D1_COLOR_F &c, float a)
{
    const float keep = 1.0f - a;
    px[0] = c.r * a + px[0] * keep;
    px[1] = c.g * a + px[1] * keep;
    px[2] = c.b * a + px[2] * keep;
    px[3] = a + px[3] * keep;
}

std::uint32_t Pack(const float px[4])
{
    const auto byte = [](float v) { return static_cast<std::uint32_t>(std::lround(std::max(0.0f, std::min(1.0f, v)) * 255.0f)); };
    return byte(px[3]) << 24 | byte(px[0]) << 16 | byte(px[1]) << 8 | byte(px[2]);
}

}  // namespace

HICON MakeAppIcon(int size, bool recording)
{
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;                        /* top-down */
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);     /* the alpha channel does the masking */
    HICON icon = nullptr;
    if (color && mask && bits) {
        const float s = static_cast<float>(size), half = s * 0.5f, corner = s * 0.24f;
        std::uint32_t *out = static_cast<std::uint32_t *>(bits);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float dx = static_cast<float>(x) + 0.5f - half, dy = static_cast<float>(y) + 0.5f - half;
                const float qx = std::fabs(dx) - (half - corner), qy = std::fabs(dy) - (half - corner);
                const float tile = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) +
                                   std::min(std::max(qx, qy), 0.0f) - corner;       /* a rounded square */
                const float radius = std::hypot(dx, dy);

                float px[4] = {};
                Over(px, theme::kHover, Coverage(tile));
                if (recording)
                    Over(px, theme::kRecord, Coverage(radius - s * 0.30f) * Coverage(tile));
                else
                    Over(px, theme::kMic, Coverage(std::fabs(radius - s * 0.27f) - s * 0.075f) * Coverage(tile));
                out[y * size + x] = Pack(px);
            }
        }
        ICONINFO parts = {TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&parts);
    }
    if (color)
        DeleteObject(color);
    if (mask)
        DeleteObject(mask);
    return icon;
}

TrayIcon::~TrayIcon()
{
    Remove();
    for (HICON icon : icons_) {
        if (icon)
            DestroyIcon(icon);
    }
}

bool TrayIcon::Register()
{
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (added_) {
        data_.uVersion = NOTIFYICON_VERSION_4;          /* NIN_SELECT and WM_CONTEXTMENU, with the position */
        Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }
    return added_;
}

bool TrayIcon::Add(HWND window, UINT message, bool recording, const std::wstring &tip)
{
    const int size = GetSystemMetrics(SM_CXSMICON);
    for (int i = 0; i < 2; ++i) {
        if (!icons_[i])
            icons_[i] = MakeAppIcon(size, i == 1);
    }

    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = window;
    data_.uID = kTrayId;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.uCallbackMessage = message;
    data_.hIcon = icons_[recording ? 1 : 0] ? icons_[recording ? 1 : 0] : LoadIconW(nullptr, IDI_APPLICATION);
    Copy(data_.szTip, ARRAYSIZE(data_.szTip), tip);
    return Register();
}

void TrayIcon::Restore()
{
    if (added_)
        Register();
}

void TrayIcon::Update(bool recording, const std::wstring &tip)
{
    if (!added_)
        return;
    data_.uFlags = NIF_ICON | NIF_TIP;
    if (icons_[recording ? 1 : 0])
        data_.hIcon = icons_[recording ? 1 : 0];
    Copy(data_.szTip, ARRAYSIZE(data_.szTip), tip);
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::Remove()
{
    if (added_)
        Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false;
}

void TrayIcon::Balloon(const std::wstring &title, const std::wstring &text)
{
    if (!added_)
        return;
    data_.uFlags = NIF_INFO;
    data_.dwInfoFlags = NIIF_WARNING;
    Copy(data_.szInfoTitle, ARRAYSIZE(data_.szInfoTitle), title);
    Copy(data_.szInfo, ARRAYSIZE(data_.szInfo), text);
    Shell_NotifyIconW(NIM_MODIFY, &data_);
    data_.szInfo[0] = L'\0';
    data_.dwInfoFlags = 0;
}

}  // namespace audiorgui::ui
