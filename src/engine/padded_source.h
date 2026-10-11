/*
 * padded_source.h - Keeps a stream as long as the recording.
 *
 * A playback device delivers no audio at all while nothing is playing, unlike
 * a microphone, which always delivers (silent) packets.  Left alone, the
 * loopback track would come out shorter than the microphone's, drifting out of
 * step in a merged file and ending up empty in a separate one.  So this wraps
 * the source and, whenever the audio it has delivered is behind the clock,
 * adds the missing time as silence (see silence_filler.h).
 */
#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "capture_source.h"
#include "pcm.h"
#include "silence_filler.h"

namespace audiorgui {

/* Seconds on a monotonic clock, from an arbitrary origin. */
double MonotonicSeconds();

class PaddedSource final : public ICaptureSource {
public:
    using Clock = std::function<double()>;              /* seconds, from any fixed origin */

    PaddedSource(std::unique_ptr<ICaptureSource> inner, Clock clock)
        : inner_(std::move(inner)), clock_(std::move(clock)) {}

    HRESULT Open() override { return inner_->Open(); }

    HRESULT Start() override
    {
        startedAt_ = clock_();
        return inner_->Start();
    }

    void Begin() override
    {
        inner_->Begin();
        startedAt_ = clock_();                          /* the audio before it is gone, and so is its time */
        filler_ = SilenceFiller();
    }

    HRESULT Poll(std::vector<std::int16_t> *frames) override
    {
        const std::size_t before = frames->size();
        const HRESULT hr = inner_->Poll(frames);
        if (FAILED(hr))
            return hr;
        const std::size_t delivered = (frames->size() - before) / kPcmChannels;
        frames->insert(frames->end(), filler_.Owed(clock_() - startedAt_, delivered) * kPcmChannels, 0);
        return S_OK;
    }

    void Stop() override { inner_->Stop(); }

private:
    std::unique_ptr<ICaptureSource> inner_;
    Clock                           clock_;
    SilenceFiller                   filler_;
    double                          startedAt_ = 0.0;
};

}  // namespace audiorgui
