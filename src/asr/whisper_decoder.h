/*
 * whisper_decoder.h - whisper.cpp behind the one call the helper needs: the
 * text of a stretch of 16 kHz audio.
 *
 * The model is told what language to expect (and, for Chinese, which script to
 * write it in), or finds the language of each utterance itself.  On the processor
 * the encoder is given only as much context as the utterance needs (it costs the
 * same for a clip of any length otherwise), which about halves the time of a short
 * phrase; on a graphics card of its own it gets its whole window and one kept
 * state (see Decode).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../core/asr_languages.h"
#include "decoder.h"

struct whisper_context;
struct whisper_state;

namespace audiorasr {

class WhisperDecoder final : public IDecoder {
public:
    WhisperDecoder() = default;
    ~WhisperDecoder() override;
    WhisperDecoder(const WhisperDecoder &) = delete;
    WhisperDecoder &operator=(const WhisperDecoder &) = delete;

    /* Loads the backends from `backendDir` and the model (a path in UTF-8).  `language`: what the speech is in
     * (Auto: the model finds out for each utterance).  `gpu`: use the graphics card if there is a backend for
     * it and a card it can use.  False, with a reason, if it cannot. */
    bool Load(const std::string &path, audiorgui::SpeechLanguage language, bool gpu, const std::string &backendDir,
              std::string *error);

    const std::string &Device() const override { return device_; }
    bool Decode(const std::int16_t *samples, std::size_t count, std::string *text) override;
    int Threads() const override { return threads_; }

private:
    whisper_context *context_ = nullptr;
    const audiorgui::SpeechLanguageInfo *language_ = &audiorgui::kSpeechLanguages[0];
    int              threads_ = 4;
    std::string      device_;
    bool             discrete_ = false;         /* computing on a graphics card of its own */
    whisper_state   *kept_ = nullptr;           /* its state, kept between decodes (see Decode) */
    std::vector<float> pcm_;                    /* reused for every utterance */
};

}  // namespace audiorasr
