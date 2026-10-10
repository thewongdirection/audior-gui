#include "app_window.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>

namespace audiorgui::ui {

namespace {

const wchar_t kClassName[] = L"AudiorGuiWindow";
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT kFrameMs = 16;                       /* about 60 frames a second */
constexpr double kMaxFrameSeconds = 0.25;           /* after a stall, do not jump */
constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW;

constexpr float kStartWidth = 1120.0f, kStartHeight = 720.0f;       /* client size, DIPs */
constexpr float kMinWidth = 900.0f, kMinHeight = 660.0f;

}  // namespace

AppWindow::AppWindow(Gfx &gfx, RecorderController &controller)
    : gfx_(gfx), ctl_(controller), painter_(gfx), view_(controller, this)
{
    QueryPerformanceFrequency(&frequency_);
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
    RECT frame = {0, 0, MulDiv(static_cast<int>(kStartWidth), static_cast<int>(systemDpi), 96),
                  MulDiv(static_cast<int>(kStartHeight), static_cast<int>(systemDpi), 96)};
    AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, systemDpi);

    hwnd_ = CreateWindowExW(0, kClassName, L"audior", kStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                            frame.right - frame.left, frame.bottom - frame.top,
                            nullptr, nullptr, instance, this);
    if (!hwnd_)
        return false;
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
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

void AppWindow::Wake()
{
    if (timerOn_)
        return;
    QueryPerformanceCounter(&lastTick_);
    timerOn_ = SetTimer(hwnd_, kFrameTimer, kFrameMs, nullptr) != 0;
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
    Invalidate();
    if (!ctl_.Animating())
        StopTimer();
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
    switch (message) {
    case WM_CREATE:
        ApplyTitleBarTheme();
        return 0;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            const int w = LOWORD(lParam), h = HIWORD(lParam);
            if (target_)
                target_->Resize(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)));
            view_.Layout(ToDip(w), ToDip(h));
            Invalidate();
        }
        return 0;

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        const RECT *suggested = reinterpret_cast<const RECT *>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (target_)
            target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        return 0;
    }

    case WM_GETMINMAXINFO: {
        RECT frame = {0, 0, ToPixels(kMinWidth), ToPixels(kMinHeight)};
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
        ReleaseCapture();                   /* before the view acts: it may open a menu */
        view_.MouseUp(ToDip(GET_X_LPARAM(lParam)), ToDip(GET_Y_LPARAM(lParam)));
        Invalidate();
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, view_.HandCursor() ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
        if (view_.KeyDown(static_cast<UINT>(wParam), GetKeyState(VK_SHIFT) < 0)) {
            Invalidate();
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == kFrameTimer)
            OnTimer();
        return 0;

    case WM_CLOSE:
    case WM_ENDSESSION:
        ctl_.Stop();                        /* never leave a recording half-written */
        break;

    case WM_DESTROY:
        StopTimer();
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

void AppWindow::ShowInFolder(const std::wstring &file)
{
    const std::wstring args = L"/select,\"" + file + L"\"";
    ShellExecuteW(hwnd_, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

}  // namespace audiorgui::ui
