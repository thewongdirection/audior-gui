/*
 * parakeet_decoder.h - NVIDIA's Parakeet TDT (in whisper.cpp's parakeet library) behind the
 * helper's IDecoder.  It finds the language of the speech by itself (25 European languages)
 * and takes no hint, so the recorder's language choice does not apply to it.  One state is kept
 * between decodes: unlike Whisper's, it does not depend on the length of the audio.
 */
#pragma once

#include <vector>

#include "decoder.h"

struct parakeet_context;
struct parakeet_state;

namespace audiorasr {

class ParakeetDecoder final : public IDecoder {
public:
    ParakeetDecoder() = default;
    ~ParakeetDecoder() override;
    ParakeetDecoder(const ParakeetDecoder &) = delete;
    ParakeetDecoder &operator=(const ParakeetDecoder &) = delete;

    /* Loads the backends from `backendDir` and the model (a path in UTF-8).  `gpu`: use the graphics
     * card if there is a backend for it and a card it can use.  False, with a reason, if it cannot. */
    bool Load(const std::string &path, bool gpu, const std::string &backendDir, std::string *error);

    const std::string &Device() const override { return device_; }
    bool Decode(const std::int16_t *samples, std::size_t count, std::string *text) override;
    int Threads() const override { return threads_; }

private:
    parakeet_context *context_ = nullptr;
    parakeet_state   *state_ = nullptr;
    int               threads_ = 4;
    std::string       device_;
    std::vector<float> pcm_;                    /* reused for every utterance */
};

}  // namespace audiorasr
