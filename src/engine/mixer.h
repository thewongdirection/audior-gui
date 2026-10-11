/*
 * mixer.h - Mixing the two streams into one file.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace audiorgui {

/* dst = a * weightA + b * (1 - weightA), per sample.  The weights add up to
 * one, so the sum can never leave the 16-bit range: a merged recording does not
 * clip at any split.  `dst` may be the same buffer as `a` or `b`. */
void MixWeighted(std::int16_t *dst, const std::int16_t *a, const std::int16_t *b, std::size_t samples,
                 float weightA);

}  // namespace audiorgui
