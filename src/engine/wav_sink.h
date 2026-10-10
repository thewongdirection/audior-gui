/*
 * wav_sink.h - Writes canonical PCM as a WAV file (48 kHz, 16-bit, stereo).
 */
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "audio_sink.h"

namespace audiorgui {

class WavSink final : public IAudioSink {
public:
    /* A WAV file stores sizes in 32 bits: at most 4 GiB (about 6 h 12 min). */
    static constexpr std::uint64_t kMaxDataBytes = 0xFFFFFFFFull - 36;

    /* Creates `path`, which must not exist.  On failure returns null and says
     * why in *hr.  `maxDataBytes` is the format's limit; tests lower it. */
    static std::unique_ptr<IAudioSink> Create(const std::wstring &path, HRESULT *hr,
                                              std::uint64_t maxDataBytes = kMaxDataBytes);

    ~WavSink() override { Close(true); }

    HRESULT       Write(const void *pcm, std::size_t bytes) override;
    HRESULT       Close(bool keep) override;
    std::uint64_t BytesWritten() const override { return dataBytes_; }

private:
    WavSink(HANDLE file, std::wstring path, std::uint64_t maxDataBytes)
        : file_(file), path_(std::move(path)), maxDataBytes_(maxDataBytes) {}
    HRESULT WriteHeader();

    HANDLE                     file_;
    std::wstring               path_;
    std::uint64_t              maxDataBytes_;
    std::atomic<std::uint64_t> dataBytes_{0};
    bool                       closed_  = false;
    HRESULT                    closeHr_ = S_OK;
};

}  // namespace audiorgui
