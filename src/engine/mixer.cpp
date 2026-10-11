#include "mixer.h"

#include <algorithm>
#include <cmath>

namespace audiorgui {

void MixWeighted(std::int16_t *dst, const std::int16_t *a, const std::int16_t *b, std::size_t samples,
                 float weightA)
{
    const float weightB = 1.0f - weightA;
    for (std::size_t i = 0; i < samples; ++i) {
        const float mixed = weightA * static_cast<float>(a[i]) + weightB * static_cast<float>(b[i]);
        dst[i] = static_cast<std::int16_t>(std::lrint(std::min(32767.0f, std::max(-32768.0f, mixed))));
    }
}

}  // namespace audiorgui
