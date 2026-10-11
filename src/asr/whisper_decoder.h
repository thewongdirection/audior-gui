/*
 * whisper_decoder.h - whisper.cpp behind the one call the helper needs: the
 * text of a stretch of 16 kHz audio.
 *
 * A model with English-only weights is told to expect English, and so is a
 * multilingual one if asked to; otherwise it finds the language of each
 * utterance itself.  On the processor the encoder is given only as much context
 * as the utterance needs (it costs the same for a clip of any length otherwise),
 * which about halves the time of a short phrase; on a graphics card of its own it
 * gets its whole window and one kept state (see Decode).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct whisper_context;
struct whisper_state;

namespace audiorasr {

class WhisperDecoder {
public:
    WhisperDecoder() = default;
    ~WhisperDecoder();
    WhisperDecoder(const WhisperDecoder &) = delete;
    WhisperDecoder &operator=(const WhisperDecoder &) = delete;

    /* Loads the backends from `backendDir` and the model (a path in UTF-8).  `english`: take the speech to
     * be English even if the model could find the language itself.  `gpu`: use the graphics card if there
     * is a backend for it and a card it can use.  False, with a reason, if it cannot. */
    bool Load(const std::string &path, bool english, bool gpu, const std::string &backendDir, std::string *error);

    /* What it computes on: "CPU", or the graphics card's name. */
    const std::string &Device() const { return device_; }

    /* The text (UTF-8, possibly empty) of `count` samples of 16 kHz mono audio,
     * at most 30 s of it.  False if the engine failed. */
    bool Decode(const std::int16_t *samples, std::size_t count, std::string *text);

    int Threads() const { return threads_; }

private:
    whisper_context *context_ = nullptr;
    bool             english_ = true;           /* told so, or a model that knows nothing else */
    int              threads_ = 4;
    std::string      device_;
    bool             discrete_ = false;         /* computing on a graphics card of its own */
    whisper_state   *kept_ = nullptr;           /* its state, kept between decodes (see Decode) */
    std::vector<float> pcm_;                    /* reused for every utterance */
};

}  // namespace audiorasr
