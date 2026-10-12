#include "fft.h"

#include <cmath>
#include <utility>

namespace audiorgui {

Fft::Fft(std::size_t size) : size_(size), reversed_(size), twiddle_(size / 2)
{
    std::size_t bits = 0;
    while ((std::size_t(1) << bits) < size)
        ++bits;
    for (std::size_t i = 0; i < size; ++i) {
        std::size_t r = 0;
        for (std::size_t b = 0; b < bits; ++b) {
            if (i & (std::size_t(1) << b))
                r |= std::size_t(1) << (bits - 1 - b);
        }
        reversed_[i] = r;
    }
    const double pi = 3.14159265358979323846;
    for (std::size_t k = 0; k < size / 2; ++k) {
        const double angle = -2.0 * pi * static_cast<double>(k) / static_cast<double>(size);
        twiddle_[k].re = static_cast<float>(std::cos(angle));
        twiddle_[k].im = static_cast<float>(std::sin(angle));
    }
}

void Fft::Transform(Complex *data, bool inverse) const
{
    for (std::size_t i = 0; i < size_; ++i) {
        const std::size_t j = reversed_[i];
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= size_; length <<= 1) {
        const std::size_t half = length / 2, step = size_ / length;
        for (std::size_t start = 0; start < size_; start += length) {
            for (std::size_t k = 0; k < half; ++k) {
                const Complex w = twiddle_[k * step];
                const float wi = inverse ? -w.im : w.im;
                Complex &a = data[start + k];
                Complex &b = data[start + k + half];
                const float tr = b.re * w.re - b.im * wi;          /* b * w */
                const float ti = b.re * wi + b.im * w.re;
                b.re = a.re - tr;
                b.im = a.im - ti;
                a.re += tr;
                a.im += ti;
            }
        }
    }
    if (inverse) {
        const float scale = 1.0f / static_cast<float>(size_);
        for (std::size_t i = 0; i < size_; ++i) {
            data[i].re *= scale;
            data[i].im *= scale;
        }
    }
}

}  // namespace audiorgui
