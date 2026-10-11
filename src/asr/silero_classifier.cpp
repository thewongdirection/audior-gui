#include "silero_classifier.h"

#include "whisper.h"

namespace audiorasr {

namespace {

constexpr int   kFrameSamples = 512;                    /* what the 16 kHz model takes in one step */
constexpr float kStartAbove = 0.5f, kStayAbove = 0.35f;

class SileroClassifier final : public audiorgui::FrameClassifier {
public:
    explicit SileroClassifier(whisper_vad_context *context) : context_(context) {}
    ~SileroClassifier() override { whisper_vad_free(context_); }

    int FrameSamples() const override { return kFrameSamples; }

    bool Speech(const std::int16_t *frame) override
    {
        for (int i = 0; i < kFrameSamples; ++i)
            pcm_[i] = static_cast<float>(frame[i]) / 32768.0f;
        if (!whisper_vad_detect_speech_no_reset(context_, pcm_, kFrameSamples) || whisper_vad_n_probs(context_) < 1)
            return speaking_ = false;
        const float probability = whisper_vad_probs(context_)[0];
        return speaking_ = probability >= (speaking_ ? kStayAbove : kStartAbove);
    }

    void Reset() override
    {
        whisper_vad_reset_state(context_);
        speaking_ = false;
    }

private:
    whisper_vad_context *context_;
    float                pcm_[kFrameSamples];
    bool                 speaking_ = false;
};

}  // namespace

audiorgui::TrackerConfig SileroTrackerConfig()
{
    audiorgui::TrackerConfig config;
    config.startFrames = 3;                             /* 96 ms of speech begins an utterance (the energy one asks for 80) */
    return config;
}

std::unique_ptr<audiorgui::FrameClassifier> MakeSileroClassifier(const std::string &modelPath)
{
    whisper_vad_context_params params = whisper_vad_default_context_params();
    params.n_threads = 1;                               /* a frame is tiny; the decoder needs the cores */
    params.use_gpu = false;
    whisper_vad_context *context = whisper_vad_init_from_file_with_params(modelPath.c_str(), params);
    return context ? std::make_unique<SileroClassifier>(context) : nullptr;
}

}  // namespace audiorasr
