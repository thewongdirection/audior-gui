#include "mp3_reader.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

#include "mp3_sink.h"
#include "pcm.h"

namespace audiorgui {

namespace {

using Microsoft::WRL::ComPtr;

constexpr DWORD     kAudioStream  = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
constexpr int       kMaxEmptyReads = 1000;      /* samples in a row that bring no audio before the file is given up as broken */
constexpr std::uint64_t kHnsPerSecond = 10000000;

class Mp3Reader final : public IPcmReader {
public:
    ~Mp3Reader() override
    {
        reader_.Reset();
        if (started_)
            MFShutdown();
    }

    HRESULT Open(const std::wstring &path)
    {
        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr))
            return hr;
        started_ = true;

        ComPtr<IMFMediaType> pcm;
        hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, reader_.GetAddressOf());
        if (SUCCEEDED(hr))
            hr = MakeAudioType(&pcm, MFAudioFormat_PCM, kPcmBytesPerSecond, kPcmFrameBytes, 16);
        if (SUCCEEDED(hr))
            hr = reader_->SetCurrentMediaType(kAudioStream, nullptr, pcm.Get());        /* the decoder is added for it */
        if (SUCCEEDED(hr))
            hr = reader_->SetStreamSelection(kAudioStream, TRUE);
        if (SUCCEEDED(hr)) {
            PROPVARIANT length;
            PropVariantInit(&length);
            if (SUCCEEDED(reader_->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &length)) &&
                length.vt == VT_UI8)
                total_ = length.uhVal.QuadPart * kPcmRate / kHnsPerSecond;
            PropVariantClear(&length);
        }
        return hr;
    }

    HRESULT Read(std::int16_t *out, std::size_t maxFrames, std::size_t *frames) override
    {
        const std::size_t wanted = maxFrames * kPcmFrameBytes;
        *frames = 0;
        for (int empty = 0; pending_.size() - used_ < wanted && !ended_;) {
            const std::size_t had = pending_.size() - used_;
            const HRESULT hr = Fetch();
            if (FAILED(hr))
                return hr;
            empty = pending_.size() - used_ == had ? empty + 1 : 0;
            if (empty >= kMaxEmptyReads)
                return E_FAIL;                              /* the reader would otherwise never answer */
        }
        const std::size_t bytes = std::min(wanted, pending_.size() - used_) / kPcmFrameBytes * kPcmFrameBytes;
        std::memcpy(out, pending_.data() + used_, bytes);
        used_ += bytes;
        *frames = bytes / kPcmFrameBytes;
        return S_OK;
    }

    std::uint64_t TotalFrames() const override { return total_; }

private:
    /* Appends the next decoded sample to what is waiting to be handed out. */
    HRESULT Fetch()
    {
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(used_));
        used_ = 0;

        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        HRESULT hr = reader_->ReadSample(kAudioStream, 0, nullptr, &flags, nullptr, sample.GetAddressOf());
        if (FAILED(hr))
            return hr;
        if (flags & MF_SOURCE_READERF_ERROR)
            return E_FAIL;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            ended_ = true;
        if (!sample)
            return S_OK;                                    /* a gap in the stream, or its end */

        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(buffer.GetAddressOf());
        BYTE *data = nullptr;
        DWORD length = 0;
        if (SUCCEEDED(hr))
            hr = buffer->Lock(&data, nullptr, &length);
        if (SUCCEEDED(hr)) {
            pending_.insert(pending_.end(), data, data + length);
            buffer->Unlock();
        }
        return hr;
    }

    ComPtr<IMFSourceReader> reader_;
    bool                    started_ = false;               /* MFStartup was called */
    bool                    ended_   = false;
    std::vector<BYTE>       pending_;                       /* decoded, not yet handed out: from used_ on */
    std::size_t             used_ = 0;
    std::uint64_t           total_ = 0;
};

}  // namespace

std::unique_ptr<IPcmReader> OpenMp3Reader(const std::wstring &path, HRESULT *hr)
{
    if (!Mp3Sink::Supported()) {
        *hr = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    auto reader = std::make_unique<Mp3Reader>();
    *hr = reader->Open(path);
    if (FAILED(*hr))
        return nullptr;
    return reader;
}

}  // namespace audiorgui
