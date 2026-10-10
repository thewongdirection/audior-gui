#include "mp3_sink.h"

#include <mferror.h>

#include <cstring>
#include <utility>

#include "pcm.h"

namespace audiorgui {

namespace {

using Microsoft::WRL::ComPtr;

constexpr long long kHnsPerSecond = 10000000;

/* An audio media type: MP3 (the file) or PCM (what we feed the encoder). */
HRESULT MakeAudioType(ComPtr<IMFMediaType> *type, const GUID &subtype, UINT32 bytesPerSecond, UINT32 blockAlign,
                      UINT32 bits)
{
    HRESULT hr = MFCreateMediaType(type->GetAddressOf());
    if (FAILED(hr))
        return hr;
    IMFMediaType *t = type->Get();
    t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    t->SetGUID(MF_MT_SUBTYPE, subtype);
    t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kPcmChannels);
    t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kPcmRate);
    t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytesPerSecond);
    t->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
    if (bits)
        t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bits);
    return S_OK;
}

}  // namespace

Mp3Sink::Mp3Sink(std::wstring path) : path_(std::move(path)) {}

Mp3Sink::~Mp3Sink() { Close(true); }

bool Mp3Sink::Supported()
{
    static const bool present = [] {
        const DWORD flags = LOAD_LIBRARY_SEARCH_SYSTEM32;
        return LoadLibraryExW(L"mfplat.dll", nullptr, flags) != nullptr &&
               LoadLibraryExW(L"mfreadwrite.dll", nullptr, flags) != nullptr;
    }();
    return present;
}

std::unique_ptr<IAudioSink> Mp3Sink::Create(const std::wstring &path, int bitrateKbps, HRESULT *hr)
{
    if (!Supported()) {
        *hr = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {      /* never overwrite */
        *hr = HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);
        return nullptr;
    }
    std::unique_ptr<Mp3Sink> sink(new Mp3Sink(path));
    *hr = sink->Open(bitrateKbps);
    if (FAILED(*hr)) {
        sink->Close(false);
        return nullptr;
    }
    return sink;
}

HRESULT Mp3Sink::Open(int bitrateKbps)
{
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr))
        return hr;
    started_ = true;

    /* The file is created here, failing if it exists, so a recording can never replace one
     * (the check in Create() only gives a friendlier early answer). */
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFMediaType> mp3, pcm;
    hr = MFCreateFile(MF_ACCESSMODE_WRITE, MF_OPENMODE_FAIL_IF_EXIST, MF_FILEFLAGS_NONE, path_.c_str(),
                      file_.GetAddressOf());
    created_ = SUCCEEDED(hr);
    if (SUCCEEDED(hr))
        hr = MFCreateAttributes(attributes.GetAddressOf(), 1);
    if (SUCCEEDED(hr))
        hr = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MP3);
    if (SUCCEEDED(hr))
        hr = MFCreateSinkWriterFromURL(nullptr, file_.Get(), attributes.Get(), writer_.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = MakeAudioType(&mp3, MFAudioFormat_MP3, static_cast<UINT32>(bitrateKbps) * 1000 / 8, 1, 0);
    if (SUCCEEDED(hr))
        hr = writer_->AddStream(mp3.Get(), &stream_);
    if (SUCCEEDED(hr))
        hr = MakeAudioType(&pcm, MFAudioFormat_PCM, kPcmBytesPerSecond, kPcmFrameBytes, 16);
    if (SUCCEEDED(hr))
        hr = writer_->SetInputMediaType(stream_, pcm.Get(), nullptr);
    if (SUCCEEDED(hr))
        hr = writer_->BeginWriting();
    return hr;
}

HRESULT Mp3Sink::Write(const void *pcm, std::size_t bytes)
{
    if (closed_ || !writer_)
        return E_FAIL;

    ComPtr<IMFMediaBuffer> buffer;
    ComPtr<IMFSample> sample;
    HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(bytes), buffer.GetAddressOf());
    BYTE *data = nullptr;
    if (SUCCEEDED(hr))
        hr = buffer->Lock(&data, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        std::memcpy(data, pcm, bytes);
        buffer->Unlock();
        hr = buffer->SetCurrentLength(static_cast<DWORD>(bytes));
    }
    if (SUCCEEDED(hr))
        hr = MFCreateSample(sample.GetAddressOf());
    if (SUCCEEDED(hr))
        hr = sample->AddBuffer(buffer.Get());

    const long long duration = static_cast<long long>(bytes) * kHnsPerSecond / kPcmBytesPerSecond;
    if (SUCCEEDED(hr))
        hr = sample->SetSampleTime(timeHns_);
    if (SUCCEEDED(hr))
        hr = sample->SetSampleDuration(duration);
    if (SUCCEEDED(hr))
        hr = writer_->WriteSample(stream_, sample.Get());
    timeHns_ += duration;

    RefreshSize();
    return hr;
}

void Mp3Sink::RefreshSize()
{
    QWORD length = 0;
    if (file_ && SUCCEEDED(file_->GetLength(&length)))
        fileBytes_ = length;
}

HRESULT Mp3Sink::Close(bool keep)
{
    if (closed_)
        return closeHr_;
    closed_ = true;
    if (writer_) {
        closeHr_ = writer_->Finalize();         /* writes the last frames */
        if (timeHns_ == 0)
            closeHr_ = S_OK;                    /* nothing was recorded, so there was nothing to finish */
        writer_.Reset();
    }
    if (file_) {
        if (keep)
            RefreshSize();
        const HRESULT closed = file_->Close();  /* the file is complete only now */
        if (SUCCEEDED(closeHr_))
            closeHr_ = closed;
        file_.Reset();
    }
    if (started_)
        MFShutdown();
    if (!keep && created_)
        DeleteFileW(path_.c_str());             /* only a file this sink made */
    return closeHr_;
}

}  // namespace audiorgui
