/*
 * tray_icon.h - The program's icon, and its place in the notification area.
 *
 * The icon is drawn in code (a dark tile with an open ring, or a solid red dot
 * while recording), so the program needs no resource file.  TrayIcon shows it
 * in the notification area while the window is hidden there, switches it to
 * the recording look, and can raise a balloon for a problem nobody would
 * otherwise see.
 */
#pragma once

#include <windows.h>

#include <shellapi.h>

#include <string>

namespace audiorgui::ui {

/* The icon at `size` pixels square; the caller destroys it.  Null on failure. */
HICON MakeAppIcon(int size, bool recording);

class TrayIcon {
public:
    TrayIcon() = default;
    TrayIcon(const TrayIcon &) = delete;
    TrayIcon &operator=(const TrayIcon &) = delete;
    ~TrayIcon();

    /* Puts the icon in the notification area.  The shell sends `message` to
     * `window` for what the user does with it (a click, the context menu).
     * False if the shell would not take it. */
    bool Add(HWND window, UINT message, bool recording, const std::wstring &tip);

    /* Puts it back after Explorer restarted and the area was rebuilt. */
    void Restore();

    void Update(bool recording, const std::wstring &tip);
    void Remove();

    /* A balloon (with a warning icon, or an information one); nothing if the icon is not shown. */
    void Balloon(const std::wstring &title, const std::wstring &text, bool warning = true);

    bool Visible() const { return added_; }

private:
    bool Register();

    NOTIFYICONDATAW data_  = {};
    HICON           icons_[2] = {};         /* idle, recording */
    bool            added_ = false;
};

}  // namespace audiorgui::ui
