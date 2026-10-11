/*
 * decoder.h - What the helper needs of a speech engine: the text of a stretch of
 * 16 kHz audio.  WhisperDecoder and ParakeetDecoder are the two that exist; the
 * recorder says in Hello which one the model file is for.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace audiorasr {

class IDecoder {
public:
    virtual ~IDecoder() = default;

    /* The text (UTF-8, possibly empty) of `count` samples of 16 kHz mono audio, at most 30 s of it.
     * False if the engine failed. */
    virtual bool Decode(const std::int16_t *samples, std::size_t count, std::string *text) = 0;

    /* What it computes on: "CPU", or the graphics card's name. */
    virtual const std::string &Device() const = 0;

    /* How many processor threads it uses (for the status text of a CPU run). */
    virtual int Threads() const = 0;
};

}  // namespace audiorasr
