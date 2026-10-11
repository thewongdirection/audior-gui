#include "padded_source.h"

namespace audiorgui {

double MonotonicSeconds()
{
    static const double ticksPerSecond = [] {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return frequency.QuadPart > 0 ? static_cast<double>(frequency.QuadPart) : 1.0;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / ticksPerSecond;
}

}  // namespace audiorgui
