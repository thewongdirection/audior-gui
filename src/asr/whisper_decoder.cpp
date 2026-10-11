#include "whisper_decoder.h"

#include <algorithm>
#include <thread>

#include "whisper.h"

namespace audiorasr {

namespace {

/* whisper.cpp refuses less than a second of audio, so a short phrase is padded with silence. */
constexpr std::size_t kMinSamples = 16160;

constexpr int kSamplesPerEncoderFrame = 320;            /* the encoder looks at 20 ms at a time */
constexpr int kEncoderMargin = 64;                      /* frames to spare, so the end of a phrase is not clipped */
constexpr int kMinEncoderFrames = 256;
constexpr int kMaxEncoderFrames = 1500;                 /* 30 s */

/* Fast speech is about four words, six tokens, a second; a decode that wants more than this is stuck. */
constexpr int kMaxTokensPerSecond = 10;
constexpr int kTokenAllowance = 12;
constexpr int kSampleRate = 16000;

void Silent(ggml_log_level, const char *, void *) {}

/* A decoding state that is freed when it goes out of scope. */
class State {
public:
    explicit State(whisper_context *context) : state_(whisper_init_state(context)) {}
    ~State()
    {
        if (state_)
            whisper_free_state(state_);
    }
    State(const State &) = delete;
    State &operator=(const State &) = delete;
    whisper_state *Get() const { return state_; }

private:
    whisper_state *state_;
};

}  // namespace

WhisperDecoder::~WhisperDecoder()
{
    if (context_)
        whisper_free(context_);
}

bool WhisperDecoder::Load(const std::string &path, bool english, std::string *error)
{
    whisper_log_set(Silent, nullptr);                   /* the helper's error output goes nowhere anyway */
    whisper_context_params params = whisper_context_default_params();
    params.use_gpu = false;
    context_ = whisper_init_from_file_with_params_no_state(path.c_str(), params);
    if (!context_) {
        *error = "The model could not be loaded.";
        return false;
    }
    english_ = english || whisper_is_multilingual(context_) == 0;

    const unsigned cores = std::thread::hardware_concurrency();     /* logical: half of them are real cores */
    threads_ = cores == 0 ? 4 : static_cast<int>(std::min(8u, std::max(2u, cores / 2)));
    return true;
}

bool WhisperDecoder::Decode(const std::int16_t *samples, std::size_t count, std::string *text)
{
    text->clear();
    const std::size_t padded = std::max(count, kMinSamples);
    pcm_.assign(padded, 0.0f);
    for (std::size_t i = 0; i < count; ++i)
        pcm_[i] = static_cast<float>(samples[i]) / 32768.0f;

    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = threads_;
    p.no_context = true;                                /* each utterance stands alone */
    p.single_segment = true;
    p.no_timestamps = true;
    p.translate = false;
    p.print_special = p.print_progress = p.print_realtime = p.print_timestamps = false;
    p.language = english_ ? "en" : "auto";
    p.detect_language = false;
    p.suppress_blank = true;
    p.suppress_nst = true;                              /* no "[MUSIC]" and the like */
    p.temperature_inc = 0.0f;                           /* no slow retries at higher temperatures */
    p.greedy.best_of = 1;
    p.max_tokens = static_cast<int>(count * kMaxTokensPerSecond / kSampleRate) + kTokenAllowance;     /* a loop gets no room */
    p.audio_ctx = std::min(kMaxEncoderFrames, std::max(kMinEncoderFrames, static_cast<int>(padded) / kSamplesPerEncoderFrame +
                                                                              kEncoderMargin));

    /* A fresh state for every decode: with a state that has served an utterance of another length (and so
     * another encoder context) the next text came out without its punctuation. */
    State state(context_);
    if (!state.Get() || whisper_full_with_state(context_, state.Get(), p, pcm_.data(), static_cast<int>(padded)) != 0)
        return false;
    for (int i = 0, n = whisper_full_n_segments_from_state(state.Get()); i < n; ++i)
        *text += whisper_full_get_segment_text_from_state(state.Get(), i);
    return true;
}

}  // namespace audiorasr
