/*
 * whisper_decoder.h - whisper.cpp behind the one call the helper needs: the
 * text of a stretch of 16 kHz audio.
 *
 * CPU only.  A model with English-only weights is told to expect English; a
 * multilingual one finds the language of each utterance itself.  The encoder is
 * given only as much context as the utterance needs (it costs the same for a
 * clip of any length otherwise), which about halves the time of a short phrase.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct whisper_context;

namespace audiorasr {

class WhisperDecoder {
public:
    WhisperDecoder() = default;
    ~WhisperDecoder();
    WhisperDecoder(const WhisperDecoder &) = delete;
    WhisperDecoder &operator=(const WhisperDecoder &) = delete;

    /* Loads the model (a path in UTF-8).  False, with a reason, if it cannot. */
    bool Load(const std::string &path, std::string *error);

    /* The text (UTF-8, possibly empty) of `count` samples of 16 kHz mono audio,
     * at most 30 s of it.  False if the engine failed. */
    bool Decode(const std::int16_t *samples, std::size_t count, std::string *text);

    int Threads() const { return threads_; }

private:
    whisper_context *context_ = nullptr;
    bool             multilingual_ = false;
    int              threads_ = 4;
    std::vector<float> pcm_;                    /* reused for every utterance */
};

}  // namespace audiorasr
