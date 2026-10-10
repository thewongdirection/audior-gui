/*
 * mp3_sink.h - Encodes canonical PCM to an MP3 file with Media Foundation's
 * in-box encoder (constant bit rate, 96 to 320 kbps at 48 kHz stereo).
 *
 * Media Foundation is delay-loaded, so the program starts even on a Windows N
 * edition without it; MP3 recording then fails with a clear message.
 */
#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <memory>
#include <string>

#include "audio_sink.h"

namespace audiorgui {

class Mp3Sink final : public IAudioSink {
public:
    /* False when Media Foundation is not installed (Windows N editions). */
    static bool Supported();

    /* Creates `path`, which must not exist.  On failure returns null and says
     * why in *hr. */
    static std::unique_ptr<IAudioSink> Create(const std::wstring &path, int bitrateKbps, HRESULT *hr);

    ~Mp3Sink() override;

    HRESULT       Write(const void *pcm, std::size_t bytes) override;
    HRESULT       Close(bool keep) override;
    std::uint64_t BytesWritten() const override { return fileBytes_; }

private:
    explicit Mp3Sink(std::wstring path);
    HRESULT Open(int bitrateKbps);
    void    RefreshSize();

    std::wstring                           path_;
    Microsoft::WRL::ComPtr<IMFByteStream>  file_;               /* created with "fail if it exists"  */
    Microsoft::WRL::ComPtr<IMFSinkWriter> writer_;
    DWORD                                  stream_  = 0;
    long long                              timeHns_ = 0;      /* where the next sample starts */
    std::atomic<std::uint64_t>             fileBytes_{0};
    bool                                   created_ = false;  /* the file is ours: remove it if discarded */
    bool                                   started_ = false;  /* MFStartup was called */
    bool                                   closed_  = false;
    HRESULT                                closeHr_ = S_OK;
};

}  // namespace audiorgui
