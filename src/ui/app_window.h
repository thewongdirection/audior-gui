/*
 * app_window.h - The top-level window.
 *
 * Owns the Direct2D window target and the frame timer, translates window
 * messages into MainView input (in DIPs), and provides the operating-system
 * services MainView asks for (IViewHost).
 *
 * Frames are driven by a timer that runs only while something is moving (a
 * recording, or a waveform still settling); an idle window uses no CPU.
 */
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "gfx.h"
#include "main_view.h"
#include "painter.h"

namespace audiorgui::ui {

class AppWindow : public IViewHost {
public:
    AppWindow(Gfx &gfx, RecorderController &controller);

    bool Create(HINSTANCE instance, int showCommand);
    int  Run();                         /* the message loop; returns the exit code */

    /* IViewHost */
    std::wstring PickFolder(const std::wstring &current) override;
    int  Choose(const std::vector<std::wstring> &items, int selected, const D2D1_RECT_F &anchor) override;
    void ShowInFolder(const std::wstring &file) override;
    void Wake() override;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HRESULT CreateTarget();
    void    DiscardTarget();
    void    Render();
    void    OnTimer();
    void    StopTimer();
    void    ApplyTitleBarTheme() const;
    void    Invalidate() const { InvalidateRect(hwnd_, nullptr, FALSE); }
    float   ToDip(int pixels) const { return static_cast<float>(pixels) * 96.0f / static_cast<float>(dpi_); }
    int     ToPixels(float dips) const;

    Gfx                &gfx_;
    RecorderController &ctl_;
    Painter             painter_;
    MainView            view_;

    HWND   hwnd_ = nullptr;
    UINT   dpi_  = 96;
    ComPtr<ID2D1HwndRenderTarget> target_;

    bool          timerOn_          = false;
    bool          trackingMouse_    = false;
    bool          releasingCapture_ = false;       /* we, not the system, are ending the capture */
    LARGE_INTEGER lastTick_      = {};
    LARGE_INTEGER frequency_     = {};
};

}  // namespace audiorgui::ui
