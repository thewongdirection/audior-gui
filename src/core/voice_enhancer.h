/*
 * voice_enhancer.h - Makes speech in a finished recording clearer.
 *
 * Two passes over the audio (48 kHz stereo floats, in pieces of any size), because a recording can only be judged
 * once it is whole:
 *
 *   1. VoiceAnalyzer listens: how loud the speech is, how loud and what shape the background noise is (taken from
 *      the quietest stretches, the pauses).  It keeps a fixed amount of data, however long the recording is.
 *   2. VoiceEnhancer processes: rumble out (80 Hz high-pass), the steady noise down (spectral Wiener denoiser, at
 *      most 12 dB, so nothing turns into holes), a little less mud and a little more presence, the speech brought
 *      to a steady level (a gain, then a gentle 2:1 pull toward the target that leaves the pauses alone), and a
 *      look-ahead limiter under -1 dBFS so nothing clips.
 *
 * The result has the length of the input.  Silence stays silence.  Plain C++: see voice_dsp.h for the blocks.
 */
#pragma once

#include <cstddef>
#include <vector>

#include "voice_dsp.h"

namespace audiorgui {

/* What the first pass found out. */
struct VoiceProfile {
    std::vector<float> noise;           /* power per bin of the background noise; all zero: none to remove     */
    double             gainDb = 0.0;    /* what brings the speech to the target level                          */
    double             gateDb = -30.0;  /* under this (dB from the target) the soft parts are not raised       */
};

class VoiceAnalyzer {
public:
    VoiceAnalyzer();

    void Add(const float *interleaved, std::size_t frames);

    /* The profile of everything added.  Call once. */
    VoiceProfile Finish();

private:
    struct Quiet {                      /* one of the quietest frames seen so far                              */
        double      power;              /* its mean square                                                     */
        std::size_t slot;               /* where its spectrum is kept                                          */
    };
    static constexpr std::size_t kQuietFrames = 128;
    static constexpr std::size_t kLevelBins   = 101;        /* 1 dB each, -100 to 0 dBFS */

    void AnalyzeFrame();

    StereoStft            stft_;
    BiquadCascade         rumble_;      /* what the chain does first, done here too                            */
    std::vector<float>    frame_;       /* the frame being filled                                              */
    std::size_t           fill_ = 0;
    std::size_t           frames_ = 0;          /* frames looked at                                            */
    std::size_t           analyzed_ = 0;        /* those with sound in them                                    */
    std::vector<Quiet>    quiet_;       /* a heap: the loudest of the quietest frames on top                   */
    std::vector<float>    spectra_;     /* kQuietFrames spectra of kBins each                                  */
    std::vector<double>   levelCount_;  /* the frames at each level                                            */
    std::vector<double>   levelPower_;  /* and their mean squares added up                                     */
};

class VoiceEnhancer {
public:
    explicit VoiceEnhancer(const VoiceProfile &profile);

    /* Appends the finished audio to `out`: it lags the input by a few milliseconds until Finish(), which hands
     * back the rest, so the whole output is as long as the whole input. */
    void Process(const float *in, std::size_t frames, std::vector<float> &out);
    void Finish(std::vector<float> &out);

private:
    void Tail(std::vector<float> &out);     /* what follows the denoiser */

    BiquadCascade      before_, after_;
    SpectralDenoiser   denoiser_;
    Leveler            leveler_;
    Limiter            limiter_;
    std::vector<float> input_, denoised_;
};

}  // namespace audiorgui
