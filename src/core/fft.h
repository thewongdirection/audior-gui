/*
 * fft.h - A complex FFT for power-of-two sizes.
 *
 * Plain radix-2, in place, single precision: all the voice enhancement needs
 * (see voice_dsp.h), with no library.  The tables are made once per size, so
 * a transform allocates nothing.
 */
#pragma once

#include <cstddef>
#include <vector>

namespace audiorgui {

struct Complex {
    float re = 0.0f, im = 0.0f;
};

class Fft {
public:
    /* `size` must be a power of two, at least 2. */
    explicit Fft(std::size_t size);

    std::size_t Size() const { return size_; }

    /* In place.  Forward is X[k] = sum x[n] e^(-2 pi i k n / N); Inverse is its
     * inverse, scaled by 1/N, so Inverse(Forward(x)) gives x back. */
    void Forward(Complex *data) const { Transform(data, false); }
    void Inverse(Complex *data) const { Transform(data, true); }

private:
    void Transform(Complex *data, bool inverse) const;

    std::size_t          size_;
    std::vector<std::size_t> reversed_;      /* bit-reversed index of each position */
    std::vector<Complex> twiddle_;           /* e^(-2 pi i k / N) for k < N/2        */
};

}  // namespace audiorgui
