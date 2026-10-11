#include "whisper_decoder.h"

#include <algorithm>
#include <memory>
#include <thread>

#include "ggml-backend.h"
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
    if (kept_)
        whisper_free_state(kept_);
    if (context_)
        whisper_free(context_);
}

bool WhisperDecoder::Load(const std::string &path, bool english, bool gpu, const std::string &backendDir,
                          std::string *error)
{
    whisper_log_set(Silent, nullptr);                   /* the helper's error output goes nowhere anyway */
    ggml_log_set(Silent, nullptr);

    /* The backends are DLLs, and only these two, by name, are ever loaded.  A missing or unusable Vulkan one
     * (no driver, no graphics card) is no problem: the processor does the work. */
    ggml_backend_load((backendDir + "\\ggml-cpu.dll").c_str());
    if (gpu)
        ggml_backend_load((backendDir + "\\ggml-vulkan.dll").c_str());

    device_ = "CPU";
    for (std::size_t i = 0; gpu && i < ggml_backend_dev_count(); ++i) {     /* the card whisper.cpp will pick: the first */
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        const enum ggml_backend_dev_type type = ggml_backend_dev_type(dev);
        if (type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU) {
            device_ = ggml_backend_dev_description(dev);
            discrete_ = type == GGML_BACKEND_DEVICE_TYPE_GPU;
            break;
        }
    }

    whisper_context_params params = whisper_context_default_params();
    params.use_gpu = gpu;
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

    /* On a graphics card of its own the model gets its whole 30 s window, always the same, and one state serves
     * every decode: allocating and freeing a state of several hundred MB for each phrase cost the card's driver a
     * thread's worth of processor time, and a shortened window makes the large model repeat itself (62% more words
     * in a minute of speech).  Elsewhere (the processor, an integrated GPU, which is no faster) the window is cut to
     * what the phrase needs, and every decode has a fresh state: with a state that has served a phrase of another
     * length (and so another window) the next text came out without its punctuation. */
    std::unique_ptr<State> fresh;
    whisper_state *state;
    if (discrete_) {
        p.audio_ctx = 0;                                /* the model's own, 30 s */
        if (!kept_)
            kept_ = whisper_init_state(context_);
        state = kept_;
    } else {
        fresh = std::make_unique<State>(context_);
        state = fresh->Get();
    }
    if (!state || whisper_full_with_state(context_, state, p, pcm_.data(), static_cast<int>(padded)) != 0)
        return false;
    for (int i = 0, n = whisper_full_n_segments_from_state(state); i < n; ++i)
        *text += whisper_full_get_segment_text_from_state(state, i);
    return true;
}

}  // namespace audiorasr
