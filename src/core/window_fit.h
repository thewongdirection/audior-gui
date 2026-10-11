/*
 * window_fit.h - Where a new window goes so that all of it can be seen.
 *
 * Windows places a window created with CW_USEDEFAULT by cascading, without asking
 * whether it fits: on a big monitor the window's bottom edge, with the Start button,
 * can end up under the taskbar.  The rule is here with no Windows headers so that it
 * is unit-tested; the window asks it once, when it is created.
 */
#pragma once

namespace audiorgui {

/* A rectangle in pixels, as Win32's RECT: right and bottom are just outside. */
struct PixelRect {
    int left = 0, top = 0, right = 0, bottom = 0;

    int  Width() const { return right - left; }
    int  Height() const { return bottom - top; }
    bool operator==(const PixelRect &o) const
    {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
};

/* `window` moved the least that puts it wholly inside `work` (the monitor's work area, without the
 * taskbar), and cut down to the work area in a direction where it is bigger than that. */
PixelRect FitIntoWorkArea(PixelRect window, const PixelRect &work);

}  // namespace audiorgui
