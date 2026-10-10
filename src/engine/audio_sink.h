/*
 * audio_sink.h - Where recorded audio goes.
 *
 * A sink receives canonical PCM (see pcm.h) and stores it in a file.  The
 * recording session writes to sinks from one thread, and reads BytesWritten()
 * from another, so that figure is kept in an atomic.
 */
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace audiorgui {

class IAudioSink {
public:
    virtual ~IAudioSink() = default;

    /* Appends canonical PCM (`bytes` is a whole number of frames). */
    virtual HRESULT Write(const void *pcm, std::size_t bytes) = 0;

    /* Finishes the file so that it is valid and complete, then closes it; with
     * keep = false the file is removed instead.  Safe to call again (it
     * returns the first result). */
    virtual HRESULT Close(bool keep) = 0;

    /* Bytes in the file so far (the PCM written, or the size of an encoded file). */
    virtual std::uint64_t BytesWritten() const = 0;
};

}  // namespace audiorgui
