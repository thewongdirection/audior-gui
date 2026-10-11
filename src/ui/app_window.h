/*
 * app_window.h - The top-level window.
 *
 * Owns the Direct2D window target and the frame timer, translates window
 * messages into MainView input (in DIPs), and provides the operating-system
 * services MainView asks for (IViewHost).
 *
 * Minimizing the window hides it in the notification area (the tray); the icon
 * restores it, shows what the recording is doing, and has a menu.  The Start
 * and Stop hotkeys are registered with the system for as long as the program
 * runs, so they work whichever program has the focus, the tray included.
 *
 * Frames are driven by a timer that runs only while something is moving (a
 * recording, or a waveform still settling); an idle window uses no CPU, and a
 * window hidden in the tray, which draws nothing, ticks a few times a second.  While
 * only the start-up set-up of transcription is at work (it takes the PC's measure, or
 * downloads a model) the timer ticks four times a second, and the window is redrawn
 * only when the set-up has news; its progress also shows in the tray's tooltip, and a
 * balloon asks for the user's answer if the window is hidden.
 */
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "../preferences.h"
#include "gfx.h"
#include "main_view.h"
#include "painter.h"
#include "tray_icon.h"

namespace audiorgui::ui {

class AppWindow : public IViewHost {
public:
    AppWindow(Gfx &gfx, RecorderController &controller);
    ~AppWindow();

    bool Create(HINSTANCE instance, int showCommand);
    int  Run();                         /* the message loop; returns the exit code */

    /* Begins recording as if the user had pressed Start (for --output). */
    void StartRecording();

    /* IViewHost */
    std::wstring PickFolder(const std::wstring &current) override;
    int  Choose(const std::vector<std::wstring> &items, int selected, const D2D1_RECT_F &anchor) override;
    void ShowInFolder(const std::wstring &file) override;
    std::wstring ClipboardText() override;
    void SetClipboardText(const std::wstring &text) override;
    void Wake() override;
    bool SetHotkey(HotkeyAction action, const Hotkey &hotkey) override;

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
    UINT    FrameMs() const;
    void    RearmTimer();
    void    EnsureMinimumHeight();

    bool    RegisterHotkey(int index, const Hotkey &hotkey);
    void    RegisterSavedHotkeys();
    void    OnHotkey(HotkeyAction action);

    void    HideToTray();
    void    LeaveTray();
    void    RestoreFromTray();
    void    ShowTrayMenu(int x, int y);
    void    SyncTray();
    void    TellTrayAboutSetup();
    void    ReportProblem();
    std::wstring TrayTip() const;

    Gfx                &gfx_;
    RecorderController &ctl_;
    Painter             painter_;
    MainView            view_;
    Preferences         prefs_;
    TrayIcon            tray_;

    HWND   hwnd_ = nullptr;
    UINT   dpi_  = 96;
    ComPtr<ID2D1HwndRenderTarget> target_;
    HICON  windowIcons_[2] = {};                        /* small, large */

    Hotkey keys_[kHotkeyActionCount];
    bool   registered_[kHotkeyActionCount] = {};

    bool          hidden_           = false;        /* in the tray, not on screen */
    Phase         lastPhase_        = Phase::Idle;  /* what the tray last showed */
    SetupPhase    lastSetupPhase_   = SetupPhase::Idle;
    long long     traySeconds_      = -1;
    int           trayPercent_      = -1;
    bool          timerOn_          = false;
    UINT          timerMs_          = 0;            /* the rate it was last set to */
    bool          wasMoving_        = false;        /* something moved at the last tick: one more frame settles it */
    bool          trackingMouse_    = false;
    bool          releasingCapture_ = false;       /* we, not the system, are ending the capture */
    LARGE_INTEGER lastTick_      = {};
    LARGE_INTEGER frequency_     = {};
};

}  // namespace audiorgui::ui
