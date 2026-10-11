#include "window_fit.h"

namespace audiorgui {

namespace {

/* One direction: [low, high) inside [workLow, workHigh). */
void FitAxis(int *low, int *high, int workLow, int workHigh)
{
    if (*high - *low > workHigh - workLow) {
        *low = workLow;                                 /* too big for the monitor: all of it, no more */
        *high = workHigh;
        return;
    }
    if (*high > workHigh) {
        *low -= *high - workHigh;
        *high = workHigh;
    }
    if (*low < workLow) {
        *high += workLow - *low;
        *low = workLow;
    }
}

}  // namespace

PixelRect FitIntoWorkArea(PixelRect window, const PixelRect &work)
{
    FitAxis(&window.left, &window.right, work.left, work.right);
    FitAxis(&window.top, &window.bottom, work.top, work.bottom);
    return window;
}

}  // namespace audiorgui
