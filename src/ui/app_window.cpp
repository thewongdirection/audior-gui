#include "app_window.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstring>
#include <cwchar>

#include "../core/format.h"

namespace audiorgui::ui {

namespace {

const wchar_t kClassName[] = L"AudiorGuiWindow";
const wchar_t kTitle[] = L"Audior-GUI";
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT kFrameMs = 16;                       /* about 60 frames a second */
constexpr UINT kHiddenMs = 200;                     /* in the tray: nothing is drawn, so just keep up with the recording */
constexpr double kMaxFrameSeconds = 0.25;           /* after a stall, do not jump */
constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW;

constexpr float kStartWidth = 1120.0f, kStartHeight = 720.0f;       /* client size, DIPs */
constexpr float kMinWidth = 900.0f, kMinHeight = 680.0f;

constexpr UINT kTrayMessage = WM_APP + 1;           /* what the shell sends for the tray icon */
enum TrayCommand : int { kShowCommand = 1, kStartCommand, kStopCommand, kExitCommand };

/* The modifier bits of core/hotkey.h are Win32's, so they go to RegisterHotKey as they are. */
static_assert(kModAlt == MOD_ALT && kModCtrl == MOD_CONTROL && kModShift == MOD_SHIFT, "hotkey modifier bits");

UINT TaskbarCreatedMessage()
{
    static const UINT message = RegisterWindowMessageW(L"TaskbarCreated");      /* Explorer restarted */
    return message;
}

}  // namespace

AppWindow::AppWindow(Gfx &gfx, RecorderController &controller)
    : gfx_(gfx), ctl_(controller), painter_(gfx), view_(controller, this)
{
    QueryPerformanceFrequency(&frequency_);
}

AppWindow::~AppWindow()
{
    for (HICON icon : windowIcons_) {
        if (icon)
            DestroyIcon(icon);
    }
}

int AppWindow::ToPixels(float dips) const
{
    return MulDiv(static_cast<int>(dips + 0.5f), static_cast<int>(dpi_), 96);
}

/* ====================================================================== */
/* Creation                                                                */
/* ====================================================================== */

bool AppWindow::Create(HINSTANCE instance, int showCommand)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc))
        return false;

    /* Size for the system DPI; WM_DPICHANGED corrects it on another monitor. */
    const UINT systemDpi = GetDpiForSystem();
    const float startHeight = std::max(kStartHeight, kMinHeight + view_.ExtraMinHeight());
    RECT frame = {0, 0, MulDiv(static_cast<int>(kStartWidth), static_cast<int>(systemDpi), 96),
                  MulDiv(static_cast<int>(startHeight), static_cast<int>(systemDpi), 96)};
    AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, systemDpi);

    hwnd_ = CreateWindowExW(0, kClassName, kTitle, kStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                            frame.right - frame.left, frame.bottom - frame.top,
                            nullptr, nullptr, instance, this);
    if (!hwnd_)
        return false;

    windowIcons_[0] = MakeAppIcon(GetSystemMetrics(SM_CXSMICON), false);
    windowIcons_[1] = MakeAppIcon(GetSystemMetrics(SM_CXICON), false);
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(windowIcons_[0]));
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(windowIcons_[1]));

    RegisterSavedHotkeys();
    ShowWindow(hwnd_, showCommand);                 /* a minimized start goes straight to the tray */
    UpdateWindow(hwnd_);
    return true;
}

void AppWindow::StartRecording()
{
    view_.StartRecording();
    Invalidate();
}

int AppWindow::Run()
{
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

/* Windows 10 (1809+) honours the dark title bar attribute; Windows 11 also
 * takes the caption colours.  Whatever a given build ignores, the window
 * simply keeps its default title bar. */
void AppWindow::ApplyTitleBarTheme() const
{
    const BOOL dark = TRUE;
    const DWORD kUseImmersiveDarkMode = 20, kUseImmersiveDarkModeLegacy = 19;
    if (FAILED(DwmSetWindowAttribute(hwnd_, kUseImmersiveDarkMode, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd_, kUseImmersiveDarkModeLegacy, &dark, sizeof(dark));

    const DWORD kCaptionColor = 35, kTextColor = 36;                 /* Windows 11 */
    const COLORREF caption = RGB(0x0E, 0x10, 0x14), text = RGB(0xE8, 0xEB, 0xF1);
    DwmSetWindowAttribute(hwnd_, kCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd_, kTextColor, &text, sizeof(text));
}

/* ====================================================================== */
/* Direct2D target                                                         */
/* ====================================================================== */

HRESULT AppWindow::CreateTarget()
{
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left),
                                         static_cast<UINT32>(rc.bottom - rc.top));
    HRESULT hr = gfx_.D2D()->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd_, size), &target_);
    if (FAILED(hr))
        return hr;
    target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
    hr = painter_.Bind(target_.Get());
    if (FAILED(hr))
        DiscardTarget();
    return hr;
}

void AppWindow::DiscardTarget()
{
    painter_.Unbind();
    target_.Reset();
}

void AppWindow::Render()
{
    if (!target_ && FAILED(CreateTarget()))
        return;
    target_->BeginDraw();
    view_.Paint(painter_);
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {    /* the device was lost: rebuild next paint */
        DiscardTarget();
        Invalidate();
    }
}

/* ====================================================================== */
/* Frame timer                                                             */
/* ====================================================================== */

UINT AppWindow::FrameMs() const
{
    return hidden_ ? kHiddenMs : kFrameMs;
}

void AppWindow::Wake()
{
    if (timerOn_)
        return;
    QueryPerformanceCounter(&lastTick_);
    timerOn_ = SetTimer(hwnd_, kFrameTimer, FrameMs(), nullptr) != 0;
}

/* The same timer at the rate that suits the window now (shown or hidden). */
void AppWindow::RearmTimer()
{
    if (timerOn_)
        SetTimer(hwnd_, kFrameTimer, FrameMs(), nullptr);
}

void AppWindow::StopTimer()
{
    if (timerOn_)
        KillTimer(hwnd_, kFrameTimer);
    timerOn_ = false;
}

void AppWindow::OnTimer()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double elapsed = static_cast<double>(now.QuadPart - lastTick_.QuadPart) /
                           static_cast<double>(frequency_.QuadPart);
    lastTick_ = now;

    ctl_.Tick(std::min(elapsed, kMaxFrameSeconds));
    view_.Update();
    SyncTray();
    if (!hidden_)
        Invalidate();
    if (!ctl_.Animating())
        StopTimer();
}

/* ====================================================================== */
/* The tray                                                                */
/* ====================================================================== */

std::wstring AppWindow::TrayTip() const
{
    if (ctl_.GetPhase() != Phase::Recording)
        return kTitle;
    return std::wstring(kTitle) + L" · recording " + FormatDuration(ctl_.Stats().seconds);
}

/* Minimizing puts the window in the notification area instead of the taskbar.
 * If the shell has no notification area, the window just stays minimized. */
void AppWindow::HideToTray()
{
    if (hidden_ || !tray_.Add(hwnd_, kTrayMessage, ctl_.GetPhase() == Phase::Recording, TrayTip()))
        return;
    hidden_ = true;
    traySeconds_ = -2;                              /* so that SyncTray() brings the tip up to date */
    lastPhase_ = ctl_.GetPhase();
    ShowWindow(hwnd_, SW_HIDE);
    RearmTimer();
}

/* The window is on screen again: the icon goes, and the frame timer speeds up. */
void AppWindow::LeaveTray()
{
    hidden_ = false;
    tray_.Remove();
    RearmTimer();
}

void AppWindow::RestoreFromTray()
{
    if (!hidden_)
        return;
    LeaveTray();
    ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
    Invalidate();
}

/* Keeps the tray icon true to the recording: its look, and the time in its tip
 * (changed once a second); and, while hidden, says if a recording that was
 * running ended with a problem, which no one would otherwise see. */
void AppWindow::SyncTray()
{
    const Phase phase = ctl_.GetPhase();
    const bool changed = phase != lastPhase_;
    if (hidden_ && changed && phase == Phase::Idle)
        ReportProblem();
    lastPhase_ = phase;
    if (!hidden_)
        return;

    const long long seconds = phase == Phase::Recording ? static_cast<long long>(ctl_.Stats().seconds) : -1;
    if (!changed && seconds == traySeconds_)
        return;
    traySeconds_ = seconds;
    tray_.Update(phase == Phase::Recording, TrayTip());
}

void AppWindow::ReportProblem()
{
    const Outcome &outcome = ctl_.LastOutcome();
    if (hidden_ && outcome.kind == Outcome::Kind::Failed)
        tray_.Balloon(L"Audior-GUI: recording problem", outcome.message);
}

void AppWindow::ShowTrayMenu(int x, int y)
{
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    const std::wstring start = L"Start recording\t" + FormatHotkey(keys_[HotkeyIndex(HotkeyAction::Start)]);
    const std::wstring stop = L"Stop\t" + FormatHotkey(keys_[HotkeyIndex(HotkeyAction::Stop)]);
    AppendMenuW(menu, MF_STRING, kShowCommand, L"Show Audior-GUI");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (view_.CanStart() ? 0 : MF_GRAYED), kStartCommand, start.c_str());
    AppendMenuW(menu, MF_STRING | (ctl_.CanStop() ? 0 : MF_GRAYED), kStopCommand, stop.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit");
    SetMenuDefaultItem(menu, kShowCommand, FALSE);

    SetForegroundWindow(hwnd_);                     /* or the menu would stay open when the user clicks elsewhere */
    const int command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, x, y, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (command) {
    case kShowCommand:  RestoreFromTray();                 break;
    case kStartCommand: OnHotkey(HotkeyAction::Start);     break;
    case kStopCommand:  OnHotkey(HotkeyAction::Stop);      break;
    case kExitCommand:  PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
    }
}

/* ====================================================================== */
/* Hotkeys                                                                 */
/* ====================================================================== */

/* Makes `hotkey` the system-wide shortcut with slot `index`, giving up the one
 * it had.  If the system refuses (another program has it), the old one is
 * put back. */
bool AppWindow::RegisterHotkey(int index, const Hotkey &hotkey)
{
    const int id = index + 1;
    const auto attempt = [&](const Hotkey &k) { return RegisterHotKey(hwnd_, id, k.modifiers | MOD_NOREPEAT, k.key) != FALSE; };
    if (registered_[index])
        UnregisterHotKey(hwnd_, id);
    if (attempt(hotkey)) {
        keys_[index] = hotkey;
        registered_[index] = true;
        return true;
    }
    registered_[index] = registered_[index] && attempt(keys_[index]);
    return false;
}

/* Registers what was saved last time (Ctrl+Alt+R and Ctrl+Alt+S the first time) and tells the view. */
void AppWindow::RegisterSavedHotkeys()
{
    for (int i = 0; i < kHotkeyActionCount; ++i) {
        const HotkeyAction action = static_cast<HotkeyAction>(i);
        keys_[i] = prefs_.LoadHotkey(action);
        RegisterHotkey(i, keys_[i]);
        view_.SetHotkey(action, keys_[i], registered_[i]);
    }
}

bool AppWindow::SetHotkey(HotkeyAction action, const Hotkey &hotkey)
{
    if (!RegisterHotkey(HotkeyIndex(action), hotkey))
        return false;
    prefs_.SaveHotkey(action, hotkey);
    return true;
}

/* A hotkey (or its tray menu item) was used: do what the Start or Stop button does. */
void AppWindow::OnHotkey(HotkeyAction action)
{
    if (action == HotkeyAction::Start) {
        const bool couldStart = view_.CanStart();
        view_.StartRecording();
        if (couldStart && ctl_.GetPhase() == Phase::Idle)       /* it did not start: the reason is the outcome */
            ReportProblem();
    } else {
        view_.StopRecording();
    }
    if (ctl_.Animating())
        Wake();
    SyncTray();
    if (!hidden_)
        Invalidate();
}

/* ====================================================================== */
/* Messages                                                                */
/* ====================================================================== */

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    AppWindow *self;
    if (message == WM_NCCREATE) {
        self = static_cast<AppWindow *>(reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        self->dpi_ = GetDpiForWindow(hwnd);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->OnMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT AppWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == TaskbarCreatedMessage()) {
        tray_.Restore();
        return 0;
    }

    switch (message) {
    case WM_CREATE:
        ApplyTitleBarTheme();
        return 0;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            HideToTray();
        } else {
            if (hidden_)                    /* another program restored the window we had put in the tray */
                LeaveTray();
            const int w = LOWORD(lParam), h = HIWORD(lParam);
            if (target_)
                target_->Resize(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)));
            view_.Layout(ToDip(w), ToDip(h));
            Invalidate();
        }
        return 0;

    case kTrayMessage:
        switch (LOWORD(lParam)) {                   /* NOTIFYICON_VERSION_4: the event, and where on the screen in wParam */
        case NIN_SELECT:
        case NIN_KEYSELECT:
            RestoreFromTray();
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu(GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
            break;
        }
        return 0;

    case WM_HOTKEY:
        if (view_.ListeningForHotkey()) {           /* the user is typing a shortcut that is already ours */
            view_.OfferHotkey({static_cast<unsigned>(LOWORD(lParam)) & (kModAlt | kModCtrl | kModShift),
                               static_cast<unsigned>(HIWORD(lParam))});
            Invalidate();
        } else if (wParam >= 1 && wParam <= kHotkeyActionCount) {
            OnHotkey(static_cast<HotkeyAction>(wParam - 1));
        }
        return 0;

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        const RECT *suggested = reinterpret_cast<const RECT *>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (target_)
            target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        Invalidate();
        return 0;
    }

    case WM_GETMINMAXINFO: {
        RECT frame = {0, 0, ToPixels(kMinWidth), ToPixels(kMinHeight + view_.ExtraMinHeight())};
        AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, dpi_);
        MINMAXINFO *info = reinterpret_cast<MINMAXINFO *>(lParam);
        info->ptMinTrackSize.x = frame.right - frame.left;
        info->ptMinTrackSize.y = frame.bottom - frame.top;
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;                           /* every pixel is painted by Direct2D */

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        Render();
        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_DISPLAYCHANGE:
        Invalidate();
        return 0;

    case WM_MOUSEMOVE:
        if (!trackingMouse_) {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd_, 0};
            trackingMouse_ = TrackMouseEvent(&track) != FALSE;
        }
        if (view_.MouseMove(ToDip(GET_X_LPARAM(lParam)), ToDip(GET_Y_LPARAM(lParam))))
            Invalidate();
        return 0;

    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        view_.MouseLeave();
        Invalidate();
        return 0;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd_);
        view_.MouseDown(ToDip(GET_X_LPARAM(lParam)), ToDip(GET_Y_LPARAM(lParam)));
        if (view_.Capturing())
            SetCapture(hwnd_);
        Invalidate();
        return 0;

    case WM_LBUTTONUP:
        releasingCapture_ = true;
        ReleaseCapture();                   /* before the view acts: it may open a menu */
        releasingCapture_ = false;
        view_.MouseUp(ToDip(GET_X_LPARAM(lParam)), ToDip(GET_Y_LPARAM(lParam)));
        Invalidate();
        return 0;

    case WM_CAPTURECHANGED:                 /* Alt+Tab, a dialog...: the press is over */
        if (!releasingCapture_) {
            view_.CancelPress();
            Invalidate();
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            const wchar_t *cursor = view_.HandCursor() ? IDC_HAND : view_.TextCursor() ? IDC_IBEAM : IDC_ARROW;
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
        /* Holding Space or Enter must not repeat the action (Start would turn
         * straight into Stop); the arrow keys do repeat, for the sliders. */
        if ((lParam & 0x40000000) && (wParam == VK_SPACE || wParam == VK_RETURN))
            return 0;
        if (view_.KeyDown(static_cast<UINT>(wParam), GetKeyState(VK_SHIFT) < 0)) {
            Invalidate();
            return 0;
        }
        break;

    case WM_SYSKEYDOWN:                     /* Alt held: only a shortcut being typed wants these */
        if (view_.ListeningForHotkey()) {
            view_.KeyDown(static_cast<UINT>(wParam), GetKeyState(VK_SHIFT) < 0);
            MSG character;                  /* the character this press produced must not open a menu or beep */
            PeekMessageW(&character, hwnd_, WM_SYSCHAR, WM_SYSCHAR, PM_REMOVE);
            Invalidate();
            return 0;
        }
        break;

    case WM_KILLFOCUS:                      /* typing elsewhere is not for a shortcut box */
        view_.WindowDeactivated();
        Invalidate();
        return 0;

    case WM_CHAR:
        if (view_.Char(static_cast<wchar_t>(wParam))) {
            Invalidate();
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == kFrameTimer)
            OnTimer();
        return 0;

    case WM_CLOSE:
        ctl_.Stop();                        /* never leave a recording half-written */
        break;

    case WM_ENDSESSION:
        if (wParam)                         /* the session really is ending */
            ctl_.Stop();
        break;

    case WM_DESTROY:
        StopTimer();
        tray_.Remove();                     /* or the icon would linger until the mouse passes over it */
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

/* ====================================================================== */
/* Operating-system services for the view                                  */
/* ====================================================================== */

std::wstring AppWindow::PickFolder(const std::wstring &current)
{
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return std::wstring();

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose where to save recordings");

    ComPtr<IShellItem> start;
    if (!current.empty() && SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&start))))
        dialog->SetFolder(start.Get());

    ComPtr<IShellItem> chosen;
    if (FAILED(dialog->Show(hwnd_)) || FAILED(dialog->GetResult(&chosen)))
        return std::wstring();

    std::wstring result;
    PWSTR path = nullptr;
    if (SUCCEEDED(chosen->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        result = path;
        CoTaskMemFree(path);
    }
    return result;
}

int AppWindow::Choose(const std::vector<std::wstring> &items, int selected, const D2D1_RECT_F &anchor)
{
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return -1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const UINT flags = MF_STRING | (static_cast<int>(i) == selected ? MF_CHECKED : 0);
        AppendMenuW(menu, flags, i + 1, items[i].c_str());      /* command ids are 1-based */
    }

    POINT where = {ToPixels(anchor.left), ToPixels(anchor.bottom) + 4};
    ClientToScreen(hwnd_, &where);
    const int command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                                         where.x, where.y, hwnd_, nullptr);
    DestroyMenu(menu);
    return command > 0 ? command - 1 : -1;
}

std::wstring AppWindow::ClipboardText()
{
    constexpr std::size_t kMaxChars = 4096;             /* more than any file name needs */
    std::wstring text;
    if (!OpenClipboard(hwnd_))
        return text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t *chars = static_cast<const wchar_t *>(GlobalLock(data))) {
            text.assign(chars, wcsnlen_s(chars, kMaxChars));
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

void AppWindow::SetClipboardText(const std::wstring &text)
{
    if (!OpenClipboard(hwnd_))
        return;
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        void *chars = GlobalLock(memory);
        if (chars) {
            std::memcpy(chars, text.c_str(), bytes);
            GlobalUnlock(memory);
        }
        if (!chars || !SetClipboardData(CF_UNICODETEXT, memory))
            GlobalFree(memory);                         /* the clipboard owns it only if it was accepted */
    }
    CloseClipboard();
}

void AppWindow::ShowInFolder(const std::wstring &file)
{
    const std::wstring args = L"/select,\"" + file + L"\"";
    ShellExecuteW(hwnd_, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

}  // namespace audiorgui::ui
