#include "utterance_tracker.h"

#include <algorithm>
#include <cmath>

namespace audiorgui {

namespace {

constexpr float kFloorFloorDb = -80.0f;         /* the noise floor is never taken to be quieter than this */

}  // namespace

UtteranceTracker::UtteranceTracker(const TrackerConfig &config) : config_(config)
{
    audio_.reserve(Samples(config.maxUtteranceMs) + static_cast<std::size_t>(config.frameSamples));
}

void UtteranceTracker::Add(const std::int16_t *samples, std::size_t count, std::uint64_t firstSample, const Sink &emit)
{
    if (haveAudio_ && firstSample != nextSample_) {     /* audio was lost (or the stream began again) */
        if (active_)
            End(emit, false);
        audio_.clear();
        frameEnergy_ = 0.0;
        frameFill_ = 0;
        loudRun_ = 0;
        haveAudio_ = false;
    }
    if (!haveAudio_) {
        audioStart_ = firstSample;
        haveAudio_ = true;
    }
    nextSample_ = firstSample + count;

    for (std::size_t i = 0; i < count; ++i) {
        audio_.push_back(samples[i]);
        frameEnergy_ += static_cast<double>(samples[i]) * samples[i];
        if (++frameFill_ == config_.frameSamples)
            OnFrame(emit);
    }
}

void UtteranceTracker::Flush(const Sink &emit)
{
    if (active_)
        End(emit, false);
}

void UtteranceTracker::OnFrame(const Sink &emit)
{
    const double rms = std::sqrt(frameEnergy_ / config_.frameSamples);
    frameEnergy_ = 0.0;
    frameFill_ = 0;
    const float db = static_cast<float>(20.0 * std::log10(rms / 32768.0 + 1e-9));
    const std::uint64_t frameEnd = audioStart_ + audio_.size();
    if (!floorSeeded_) {
        floorDb_ = std::max(db, kFloorFloorDb);
        floorSeeded_ = true;
    }

    const bool loud = db > floorDb_ + config_.loudAboveFloorDb && db > config_.minLoudDb;
    /* The floor follows the room: down fast, up slowly, and up very slowly even while it is loud, so a fan
     * that switches on is taken for the room after a few seconds (speech has pauses, which pull it down again). */
    floorDb_ += (loud ? 0.002f : db < floorDb_ ? 0.25f : 0.01f) * (db - floorDb_);
    floorDb_ = std::max(floorDb_, kFloorFloorDb);

    if (!active_) {
        if (!loud) {
            loudRun_ = 0;
            TrimIdle();
        } else if (++loudRun_ >= config_.startFrames) {
            const std::uint64_t loudStart = frameEnd - static_cast<std::uint64_t>(loudRun_) * config_.frameSamples;
            const std::uint64_t wanted = loudStart > Samples(config_.preRollMs) ? loudStart - Samples(config_.preRollMs) : 0;
            Begin(std::max(wanted, audioStart_));
            loudSamples_ = static_cast<std::size_t>(loudRun_) * config_.frameSamples;
            lastLoudEnd_ = frameEnd;
            loudRun_ = 0;
        }
        return;
    }

    if (loud) {
        quietRun_ = 0;
        loudSamples_ += config_.frameSamples;
        lastLoudEnd_ = frameEnd;
    } else {
        ++quietRun_;
    }

    if (loudSamples_ >= Samples(config_.minSpeechMs) &&
        audio_.size() >= (requested_ == 0 ? Samples(config_.firstPartialMs) : requested_ + Samples(config_.partialEveryMs))) {
        requested_ = audio_.size();
        emit(Step{Step::Kind::Partial, id_, audioStart_, audio_.data(), audio_.size()});
    }

    if (static_cast<std::size_t>(quietRun_) * config_.frameSamples >= Samples(config_.endSilenceMs))
        End(emit, false);
    else if (audio_.size() >= Samples(config_.maxUtteranceMs))
        End(emit, true);
}

/* An utterance begins at `startSample` (a number within audio_). */
void UtteranceTracker::Begin(std::uint64_t startSample)
{
    audio_.erase(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(startSample - audioStart_));
    audioStart_ = startSample;
    active_ = true;
    id_ = nextId_++;
    quietRun_ = 0;
    loudSamples_ = 0;
    requested_ = 0;
}

/* The utterance ends: a Final if it held enough speech.  Cut at the maximum length, the speech goes on
 * and the next utterance begins at once. */
void UtteranceTracker::End(const Sink &emit, bool cutAtLength)
{
    std::size_t keep = audio_.size();
    if (!cutAtLength)                                   /* the silence after the speech is not sent: only a little of it */
        keep = std::min<std::size_t>(keep, static_cast<std::size_t>(lastLoudEnd_ - audioStart_) + Samples(config_.postRollMs));
    if (loudSamples_ >= Samples(config_.minSpeechMs))
        emit(Step{Step::Kind::Final, id_, audioStart_, audio_.data(), keep});

    audio_.erase(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(keep));
    audioStart_ += keep;
    active_ = false;
    loudRun_ = 0;
    if (cutAtLength) {
        Begin(audioStart_);
        lastLoudEnd_ = audioStart_;
    } else {
        TrimIdle();
    }
}

/* Idle, only the pre-roll is worth keeping. */
void UtteranceTracker::TrimIdle()
{
    const std::size_t keep = Samples(config_.preRollMs) + static_cast<std::size_t>(config_.frameSamples);
    if (audio_.size() > keep) {
        const std::size_t drop = audio_.size() - keep;
        audio_.erase(audio_.begin(), audio_.begin() + static_cast<std::ptrdiff_t>(drop));
        audioStart_ += drop;
    }
}

}  // namespace audiorgui
