/*
 * peak_meter.h - A peak-hold level meter with a clip indicator.
 *
 * Feed it the peak of newly heard audio and advance it with the elapsed
 * time: the level jumps up at once and falls back at a steady rate, and
 * the clip flag stays lit for a moment after the signal touched full scale.
 */
#pragma once

namespace audiorgui {

class PeakMeter {
public:
    static constexpr float  kFallDbPerSecond = 24.0f;
    static constexpr float  kFloorDb         = -90.0f;      /* below this: silence */
    static constexpr float  kClipLinear      = 0.999f;      /* about -0.01 dBFS    */
    static constexpr double kClipHoldSeconds = 1.5;

    void Reset();

    /* The largest |sample| (0..1) in audio that has just been heard. */
    void Feed(float peakLinear);

    /* Let time pass: the level falls and the clip hold runs down. */
    void Advance(double dtSeconds);

    /* Held peak level in dBFS; -infinity once it has fallen to silence. */
    float LevelDb() const;
    bool  Clipping() const { return clipHold_ > 0.0; }

    /* True while the meter still has something to count down. */
    bool  Active() const { return levelDb_ > kFloorDb || clipHold_ > 0.0; }

private:
    float  levelDb_ = kFloorDb;
    double clipHold_ = 0.0;
};

}  // namespace audiorgui
