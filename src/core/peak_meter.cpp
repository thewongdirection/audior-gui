#include "peak_meter.h"

#include <algorithm>
#include <limits>

#include "format.h"

namespace audiorgui {

void PeakMeter::Reset()
{
    levelDb_ = kFloorDb;
    clipHold_ = 0.0;
}

void PeakMeter::Feed(float peakLinear)
{
    levelDb_ = std::max(levelDb_, LinearToDb(peakLinear));
    if (peakLinear >= kClipLinear)
        clipHold_ = kClipHoldSeconds;
}

void PeakMeter::Advance(double dtSeconds)
{
    if (dtSeconds <= 0.0)
        return;
    levelDb_ = std::max(kFloorDb, levelDb_ - kFallDbPerSecond * static_cast<float>(dtSeconds));
    clipHold_ = std::max(0.0, clipHold_ - dtSeconds);
}

float PeakMeter::LevelDb() const
{
    return levelDb_ > kFloorDb ? levelDb_ : -std::numeric_limits<float>::infinity();
}

}  // namespace audiorgui
