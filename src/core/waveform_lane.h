/*
 * waveform_lane.h - The model behind one scrolling waveform.
 *
 * An engine delivers peak bins in bursts (audior flushes about four times a
 * second), but the picture must scroll smoothly.  The lane therefore keeps a
 * playhead that glides through the received bins at the real-time rate:
 *
 *   - it never runs ahead of the data that has arrived;
 *   - it never lags more than `maxLagSeconds` behind it (it skips forward).
 *
 * The waveform is shown as a row of bars.  Bar `n` always covers bins
 * [n * binsPerBar, (n + 1) * binsPerBar), so a bar's height never changes
 * once it has fully passed; the row just slides left as the playhead moves.
 */
#pragma once

#include <cstddef>
#include <vector>

#include "peak_history.h"
#include "peak_meter.h"
#include "types.h"

namespace audiorgui {

struct WaveformConfig {
    int    windowSeconds = 12;      /* history kept: at least what the view shows */
    int    binsPerBar    = 6;       /* 60 ms per bar                              */
    double maxLagSeconds = 0.6;
};

/* The bottom of the bar-height scale: quieter sounds are drawn at height 0. */
constexpr float kHeightFloorDb = -54.0f;

/* Bar height (0..1) for a linear amplitude: a dB scale from kHeightFloorDb
 * up to full scale, which reads like loudness better than a linear one does. */
float AmplitudeToHeight(float linear);

class WaveformLane {
public:
    WaveformLane();
    explicit WaveformLane(const WaveformConfig &config);

    /* Forget everything: a new recording starts from silence. */
    void Reset();

    /* New audio from the engine. */
    void Feed(const PeakBin *bins, std::size_t count);

    /* Let `dtSeconds` of real time pass. */
    void Advance(double dtSeconds);

    /* The most bars Bars() can show: the history covers `windowSeconds`. */
    std::size_t MaxBars() const;

    /* Heights (0..1, see AmplitudeToHeight) of the `count` newest bars,
     * oldest first (`count` up to MaxBars()).  Returns how far the newest bar
     * is revealed (0..1): the caller slides the row left by that fraction of
     * a bar's width.  `heights` is reused, so a steady `count` allocates
     * nothing. */
    double Bars(std::size_t count, std::vector<float> *heights) const;

    float LevelDb() const { return meter_.LevelDb(); }
    bool  Clipping() const { return meter_.Clipping(); }
    bool  HasAudio() const { return history_.Total() > 0; }

    /* True while the picture is still changing on its own: the playhead is
     * catching up with received audio, or the meter is counting down.  When
     * it is false (and no audio is arriving) nothing needs redrawing. */
    bool  Active() const { return playhead_ < static_cast<double>(history_.Total()) || meter_.Active(); }

    /* The playhead, in bins (exposed for tests). */
    double Playhead() const { return playhead_; }

private:
    std::uint64_t Revealed() const { return static_cast<std::uint64_t>(playhead_); }

    WaveformConfig config_;
    PeakHistory    history_;
    PeakMeter      meter_;
    double         playhead_ = 0.0;
};

}  // namespace audiorgui
