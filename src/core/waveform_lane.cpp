#include "waveform_lane.h"

#include <algorithm>
#include <cmath>

#include "format.h"

namespace audiorgui {

float AmplitudeToHeight(float linear)
{
    const float db = LinearToDb(std::fabs(linear));
    if (!std::isfinite(db))
        return 0.0f;
    return std::min(1.0f, std::max(0.0f, (db - kHeightFloorDb) / -kHeightFloorDb));
}

namespace {

WaveformConfig Normalized(WaveformConfig c)
{
    c.windowSeconds = std::max(c.windowSeconds, 1);
    c.binsPerBar    = std::max(c.binsPerBar, 1);
    c.maxLagSeconds = std::max(c.maxLagSeconds, 0.0);
    return c;
}

/* What the view can reach back to: its window, measured from a playhead that
 * may trail the newest audio by up to the maximum lag, plus a bar at each end
 * for the partly revealed bars. */
std::size_t HistoryCapacity(const WaveformConfig &c)
{
    return static_cast<std::size_t>(c.windowSeconds) * kPeakBinsPerSecond +
           static_cast<std::size_t>(std::ceil(c.maxLagSeconds * kPeakBinsPerSecond)) +
           static_cast<std::size_t>(c.binsPerBar) * 2;
}

}  // namespace

WaveformLane::WaveformLane() : WaveformLane(WaveformConfig()) {}

WaveformLane::WaveformLane(const WaveformConfig &config)
    : config_(Normalized(config)), history_(HistoryCapacity(config_))
{
}

void WaveformLane::Reset()
{
    history_.Clear();
    meter_.Reset();
    playhead_ = 0.0;
}

void WaveformLane::Feed(const PeakBin *bins, std::size_t count)
{
    history_.Append(bins, count);
}

void WaveformLane::Advance(double dtSeconds)
{
    const std::uint64_t before = Revealed();

    if (dtSeconds > 0.0)
        playhead_ += dtSeconds * kPeakBinsPerSecond;

    const double available = static_cast<double>(history_.Total());
    const double maxLag    = config_.maxLagSeconds * kPeakBinsPerSecond;
    playhead_ = std::min(playhead_, available);
    playhead_ = std::max(playhead_, available - maxLag);

    /* The meter reports what has just passed the playhead, so it stays in
     * step with the picture. */
    meter_.Advance(dtSeconds);
    const PeakBin heard = history_.Span(before, Revealed());
    meter_.Feed(std::max(-heard.low, heard.high));
}

std::size_t WaveformLane::MaxBars() const
{
    return static_cast<std::size_t>(config_.windowSeconds) * kPeakBinsPerSecond /
           static_cast<std::size_t>(config_.binsPerBar);
}

double WaveformLane::Bars(std::size_t count, std::vector<float> *heights) const
{
    heights->assign(count, 0.0f);

    const std::uint64_t perBar   = static_cast<std::uint64_t>(config_.binsPerBar);
    const std::uint64_t revealed = Revealed();
    const std::uint64_t newest   = revealed / perBar;       /* the partial bar */

    for (std::size_t i = 0; i < count; ++i) {
        const std::uint64_t back = count - 1 - i;
        if (back > newest)
            continue;                                       /* before the start */
        const std::uint64_t first = (newest - back) * perBar;
        const std::uint64_t last  = std::min(first + perBar, revealed);
        const PeakBin peak = history_.Span(first, last);
        (*heights)[i] = AmplitudeToHeight(std::max(-peak.low, peak.high));
    }
    return (playhead_ - static_cast<double>(newest * perBar)) / static_cast<double>(perBar);
}

}  // namespace audiorgui
