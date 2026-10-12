/*
 * voice_dsp.h - The signal processing blocks of the voice enhancement (voice_enhancer.h).
 *
 * Plain C++ on 48 kHz stereo floats (-1..+1), interleaved, so it is tested without audio hardware.  Every block
 * that delays the audio (the denoiser, the limiter) is streaming: it takes the audio in pieces of any size, hands
 * back what is finished, and after Finish() has handed back exactly as many frames as it was given.  The blocks
 * that do not delay it (the filters, the leveler) work in place.  Nothing here allocates once it is running.
 */
#pragma once

#include <cstddef>
#include <vector>

#include "fft.h"

namespace audiorgui {

/* The rate the recorder makes every file at (engine/pcm.h checks that the two agree). */
constexpr double kVoiceRate = 48000.0;

/* A chain of second-order filters (the RBJ cookbook shapes), the same for the left and the right channel. */
class BiquadCascade {
public:
    void AddHighPass(double hz, double q);
    void AddPeaking(double hz, double q, double gainDb);

    void Process(float *interleaved, std::size_t frames);

private:
    struct Section {
        double b0, b1, b2, a1, a2;          /* normalised so that a0 = 1                      */
        double z1[2] = {0.0, 0.0};          /* the state, per channel (transposed direct II)  */
        double z2[2] = {0.0, 0.0};
    };
    std::vector<Section> sections_;
};

/* The short-time spectrum of a stereo signal: frames of kSize samples, one every kHop, with a window that makes
 * analysis and synthesis together reconstruct the signal exactly.  The left channel rides in the real part and
 * the right one in the imaginary part of the same transform, so one FFT does both. */
class StereoStft {
public:
    static constexpr std::size_t kSize = 1024;
    static constexpr std::size_t kHop  = 256;
    static constexpr std::size_t kBins = kSize / 2 + 1;     /* 0 Hz to Nyquist */

    StereoStft();

    /* Windows and transforms the `kSize` interleaved frames at `frame`. */
    void Analyze(const float *frame);

    /* The mean power of the two channels in bin `k` of the last frame analysed. */
    float Power(std::size_t k) const;

    /* Multiplies bin k of that frame by gains[k] (the same for both channels). */
    void Apply(const float *gains);

    /* Transforms back, windows, and adds the frame to `overlap` (kSize interleaved frames). */
    void Synthesize(float *overlap);

private:
    Fft                fft_;
    std::vector<float> window_;
    std::vector<Complex> bins_;
};

/* Takes the steady noise out: a Wiener gain per bin, from the signal-to-noise ratio of each frame (smoothed over
 * time by the decision-directed rule, and over frequency, against "musical" noise), never lower than a floor so
 * that nothing is cut to a hole.  `noise` is the noise power per bin as the analyzer measured it; all zero leaves
 * the audio as it is. */
class SpectralDenoiser {
public:
    SpectralDenoiser(const std::vector<float> &noise, double floorDb);

    void Process(const float *in, std::size_t frames, std::vector<float> &out);     /* appends to out */
    void Finish(std::vector<float> &out);

private:
    void Hop(std::vector<float> &out);

    StereoStft         stft_;
    std::vector<float> noise_;
    float              floor_;
    std::vector<float> frame_;          /* the last kSize frames of input                       */
    std::vector<float> overlap_;        /* the output being assembled                           */
    std::vector<float> pending_;        /* input waiting for a whole hop                        */
    std::vector<float> previous_;       /* per bin: the previous frame's (gain^2 * SNR)          */
    std::vector<float> gains_;
    std::vector<float> raw_;
    std::vector<float> ratio_;
    std::size_t        pendingFrames_ = 0;
    std::size_t        skip_;           /* output still to drop: the start-up of the first frames */
    std::size_t        consumed_ = 0, emitted_ = 0;
};

/* Brings the level of the speech to a target: a fixed gain, then a gentle 2:1 pull of the loud and the soft parts
 * toward the target.  What is below the gate (the pauses, where only noise is left) is not raised. */
class Leveler {
public:
    /* staticGainDb: the fixed gain.  gateDb: the level (in dB from the target, after the fixed gain) under which
     * the soft parts are no longer raised. */
    Leveler(double staticGainDb, double gateDb);

    void Process(float *interleaved, std::size_t frames);

    static constexpr double kTargetDb = -17.0;      /* the level of active speech, dB full scale (RMS) */

private:
    static constexpr std::size_t kControlFrames = 32;

    double staticDb_, gateDb_;
    double power_ = 0.0;                /* the power of the input over the last few milliseconds */
    double envelope_ = 0.0;             /* ... followed quickly upward and slowly downward       */
    double gain_;                       /* the gain in use, linear                           */
    double step_ = 0.0;
    std::size_t untilControl_ = 0;
};

/* A look-ahead peak limiter: the gain comes down before a peak arrives and goes back slowly afterwards, so that
 * nothing passes the ceiling and nothing is clipped.  The output is delayed by kLookahead frames; the
 * count in equals the count out after Finish(). */
class Limiter {
public:
    static constexpr std::size_t kLookahead = 96;       /* 2 ms */

    explicit Limiter(float ceiling);

    void Process(const float *in, std::size_t frames, std::vector<float> &out);     /* appends to out */
    void Finish(std::vector<float> &out);

private:
    void Push(float left, float right, std::vector<float> *out);

    struct Entry {
        std::size_t index;
        float       value;
    };

    float                ceiling_;
    double               release_;          /* per frame; in double, so the gain gets all the way back to 1 */
    std::vector<float>   audio_;        /* the last kLookahead + 1 frames of input                          */
    std::vector<Entry>   window_;       /* sliding minimum of the wanted gain: increasing values, a ring   */
    std::size_t          head_ = 0, size_ = 0;
    std::vector<float>   minima_;       /* the last kLookahead + 1 minima, averaged for the smooth gain     */
    double               sum_;
    double               gain_ = 1.0;
    std::size_t          position_ = 0; /* frames pushed, the virtual start-up included                     */
    std::size_t          skip_;
    std::size_t          consumed_ = 0, emitted_ = 0;
};

}  // namespace audiorgui
