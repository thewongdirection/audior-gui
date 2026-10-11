#include "parakeet_decoder.h"

#include <algorithm>

#include "backends.h"
#include "ggml.h"
#include "parakeet.h"

namespace audiorasr {

namespace {

/* Like Whisper, it is given at least a second: a shorter phrase is padded with silence. */
constexpr std::size_t kMinSamples = 16160;

void Silent(ggml_log_level, const char *, void *) {}

}  // namespace

ParakeetDecoder::~ParakeetDecoder()
{
    if (state_)
        parakeet_free_state(state_);
    if (context_)
        parakeet_free(context_);
}

bool ParakeetDecoder::Load(const std::string &path, bool gpu, const std::wstring &backendDir, std::string *error)
{
    parakeet_log_set(Silent, nullptr);
    const BackendInfo backends = LoadBackends(backendDir, gpu);
    if (!backends.usable) {
        *error = "The processor backend of the engine could not be loaded.";
        return false;
    }
    device_ = backends.device;
    threads_ = backends.threads;

    parakeet_context_params params = parakeet_context_default_params();
    params.use_gpu = gpu;
    context_ = parakeet_init_from_file_with_params_no_state(path.c_str(), params);
    if (context_)
        state_ = parakeet_init_state(context_);
    if (!context_ || !state_) {
        *error = "The model could not be loaded.";
        return false;
    }
    return true;
}

bool ParakeetDecoder::Decode(const std::int16_t *samples, std::size_t count, std::string *text)
{
    text->clear();
    const std::size_t padded = std::max(count, kMinSamples);
    pcm_.assign(padded, 0.0f);
    for (std::size_t i = 0; i < count; ++i)
        pcm_[i] = static_cast<float>(samples[i]) / 32768.0f;

    parakeet_full_params p = parakeet_full_default_params(PARAKEET_SAMPLING_GREEDY);
    p.n_threads = threads_;
    p.no_context = true;                                /* each utterance stands alone */
    if (parakeet_full_with_state(context_, state_, p, pcm_.data(), static_cast<int>(padded)) != 0)
        return false;
    for (int i = 0, n = parakeet_full_n_segments_from_state(state_); i < n; ++i)
        *text += parakeet_full_get_segment_text_from_state(state_, i);
    return true;
}

}  // namespace audiorasr
