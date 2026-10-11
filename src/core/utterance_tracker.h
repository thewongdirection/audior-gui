/*
 * utterance_tracker.h - Cuts a stream of 16 kHz audio into utterances.
 *
 * A speech recogniser works on a phrase at a time: this decides where one
 * starts and ends, so the helper can show text while the phrase is still being
 * spoken and settle it when the speaker pauses.  Whether someone is speaking
 * is decided a frame at a time by a FrameClassifier (by default the loudness of
 * 20 ms frames against a noise floor that follows the room), with hysteresis
 * at both ends:
 *
 *   - speech starts after a short run of speaking frames, and the utterance
 *     includes a little audio from before that (so a soft first consonant is
 *     not lost);
 *   - while it goes on, a Partial step is handed out about once a second
 *     carrying all the audio so far;
 *   - it ends after a pause, with a Final step; a burst too short to be speech
 *     is dropped without a word;
 *   - an utterance is cut at a maximum length (the recogniser's window) and the
 *     next one begins where it ended.
 *
 * The tracker works in absolute sample numbers.  If the audio it is given
 * skips ahead (the recorder dropped some), the utterance in progress is ended
 * there.  Plain C++, tested with generated audio.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace audiorgui {

struct TrackerConfig {
    int   frameSamples    = 320;        /* 20 ms at 16 kHz (the energy classifier's frame; see FrameClassifier) */
    int   startFrames     = 4;          /* speaking frames in a row that begin an utterance */
    int   endSilenceMs    = 600;        /* a pause this long ends it                    */
    int   preRollMs       = 240;        /* audio kept from before the speech began      */
    int   postRollMs      = 160;        /* ... and after the last speaking frame        */
    int   minSpeechMs     = 300;        /* less speech than this is not an utterance    */
    int   firstPartialMs  = 800;        /* when the first Partial comes                 */
    int   partialEveryMs  = 1000;       /* and how often the next ones                  */
    int   maxUtteranceMs  = 15000;
    float loudAboveFloorDb = 12.0f;     /* (energy classifier) how far above the noise floor counts as loud */
    float minLoudDb       = -48.0f;     /* (energy classifier) and never quieter than this (dBFS)           */
};

/* Says, a frame of audio at a time, whether someone is speaking in it. */
class FrameClassifier {
public:
    virtual ~FrameClassifier() = default;

    virtual int FrameSamples() const = 0;

    /* The next frame of the stream: exactly FrameSamples() samples. */
    virtual bool Speech(const std::int16_t *frame) = 0;

    /* The audio jumped: forget what came before. */
    virtual void Reset() {}
};

/* Loudness against a noise floor that follows the room.  Needs no model. */
class EnergyClassifier final : public FrameClassifier {
public:
    explicit EnergyClassifier(const TrackerConfig &config = TrackerConfig()) : config_(config) {}

    int  FrameSamples() const override { return config_.frameSamples; }
    bool Speech(const std::int16_t *frame) override;

private:
    TrackerConfig config_;
    float         floorDb_ = -60.0f;
    bool          floorSeeded_ = false;
};

class UtteranceTracker {
public:
    struct Step {
        enum class Kind { Partial, Final };

        Kind                kind;
        std::uint32_t       id;             /* 0, 1, 2 ... in the order the utterances begin */
        std::uint64_t       startSample;    /* the stream's sample number of audio[0]        */
        const std::int16_t *audio;          /* valid only during the call                    */
        std::size_t         count;
    };
    using Sink = std::function<void(const Step &)>;

    /* Without a classifier, the energy one decides.  Otherwise its frame is the tracker's
     * (and `config.frameSamples` is not used). */
    explicit UtteranceTracker(const TrackerConfig &config = TrackerConfig(),
                              std::unique_ptr<FrameClassifier> classifier = nullptr);

    /* The next audio of the stream: `firstSample` is the stream's number of
     * samples[0].  Steps are handed to `emit` as they are decided. */
    void Add(const std::int16_t *samples, std::size_t count, std::uint64_t firstSample, const Sink &emit);

    /* The audio is over: an utterance in progress ends. */
    void Flush(const Sink &emit);

private:
    void OnFrame(const Sink &emit);
    void Begin(std::uint64_t startSample);
    void End(const Sink &emit, bool cutAtLength);
    void TrimIdle();
    std::size_t Samples(int milliseconds) const { return static_cast<std::size_t>(milliseconds) * 16; }

    TrackerConfig                    config_;
    std::unique_ptr<FrameClassifier> classifier_;
    std::vector<std::int16_t> audio_;           /* what is kept: the pre-roll while idle, the utterance so far while active */
    std::uint64_t             audioStart_ = 0;  /* the stream's number of audio_[0]       */
    std::uint64_t             nextSample_ = 0;  /* the number the next sample must have   */
    bool                      haveAudio_ = false;

    int           frameFill_ = 0;               /* samples of the frame being filled */

    bool          active_ = false;
    int           loudRun_ = 0;                 /* speaking frames in a row, before an utterance begins */
    int           quietRun_ = 0;                /* quiet frames in a row, during one                */
    std::uint32_t nextId_ = 0, id_ = 0;
    std::size_t   loudSamples_ = 0;             /* speech in the utterance so far                   */
    std::uint64_t lastLoudEnd_ = 0;
    std::size_t   requested_ = 0;               /* the length of audio_ at the last Partial         */
};

}  // namespace audiorgui
