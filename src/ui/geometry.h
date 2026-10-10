/*
 * geometry.h - Rectangle helpers.  All UI geometry is in DIPs (1/96 inch).
 */
#pragma once

#include <windows.h>   /* before d2d1.h: DrawText must be a macro for both the declaration and the calls */

#include <d2d1.h>

namespace audiorgui::ui {

inline D2D1_RECT_F RectF(float left, float top, float right, float bottom)
{
    return D2D1_RECT_F{left, top, right, bottom};
}

inline D2D1_RECT_F RectXYWH(float x, float y, float w, float h) { return RectF(x, y, x + w, y + h); }

inline float Width(const D2D1_RECT_F &r) { return r.right - r.left; }
inline float Height(const D2D1_RECT_F &r) { return r.bottom - r.top; }

inline D2D1_RECT_F Inflate(const D2D1_RECT_F &r, float d)
{
    return RectF(r.left - d, r.top - d, r.right + d, r.bottom + d);
}

inline bool Contains(const D2D1_RECT_F &r, float x, float y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

}  // namespace audiorgui::ui
