/*
 * pcm_reader.h - Where the audio of a finished recording is read from.
 *
 * The reverse of audio_sink.h: a reader hands out canonical PCM (see pcm.h: 48 kHz, 16-bit, stereo) from a file the
 * recorder wrote, a WAV file as it is or an MP3 file decoded by Media Foundation (mp3_reader.h).  The voice
 * enhancement (voice_enhance_job.h) reads each recording twice, so it opens it twice.
 */
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace audiorgui {

class IPcmReader {
public:
    virtual ~IPcmReader() = default;

    /* Reads up to `maxFrames` frames (interleaved 16-bit left, right) into `out`.  *frames is how many it got:
     * fewer than asked only at the end of the audio, and then 0 on the next call. */
    virtual HRESULT Read(std::int16_t *out, std::size_t maxFrames, std::size_t *frames) = 0;

    /* How many frames the file holds, as far as it says (for a progress figure); 0 if it does not. */
    virtual std::uint64_t TotalFrames() const = 0;
};

/* Opens a WAV file of the recorder's own format.  On failure returns null and says why in *hr. */
std::unique_ptr<IPcmReader> OpenWavReader(const std::wstring &path, HRESULT *hr);

/* Opens a recording by its extension (.wav or .mp3). */
std::unique_ptr<IPcmReader> OpenPcmReader(const std::wstring &path, HRESULT *hr);

}  // namespace audiorgui
