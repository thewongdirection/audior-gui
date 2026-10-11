#include "wasapi_capture.h"

#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <utility>

namespace audiorgui {

namespace {

using Microsoft::WRL::ComPtr;

constexpr REFERENCE_TIME kBufferHns = 10000000;         /* device buffer: one second */

/* KSDATAFORMAT_SUBTYPE_PCM and _IEEE_FLOAT: the format tag in the first field. */
constexpr GUID kSubtypePcm   = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

bool DescribeFormat(const WAVEFORMATEX &wf, SourceFormat *out)
{
    out->sampleRate = wf.nSamplesPerSec;
    out->channels = wf.nChannels;
    out->bitsPerSample = wf.wBitsPerSample;
    out->channelMask = 0;

    if (wf.wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const WAVEFORMATEXTENSIBLE &ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE &>(wf);
        out->channelMask = ext.dwChannelMask;
        if (ext.SubFormat == kSubtypeFloat)
            out->isFloat = true;
        else if (ext.SubFormat == kSubtypePcm)
            out->isFloat = false;
        else
            return false;
    } else if (wf.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        out->isFloat = true;
    } else if (wf.wFormatTag == WAVE_FORMAT_PCM) {
        out->isFloat = false;
    } else {
        return false;
    }
    return true;
}

}  // namespace

WasapiCapture::WasapiCapture(std::wstring deviceId, bool loopback, float gain)
    : deviceId_(std::move(deviceId)), loopback_(loopback), gain_(gain)
{
}

HRESULT WasapiCapture::Open()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));

    ComPtr<IMMDevice> device;
    if (SUCCEEDED(hr)) {
        hr = deviceId_.empty()
                 ? enumerator->GetDefaultAudioEndpoint(loopback_ ? eRender : eCapture, eConsole, device.GetAddressOf())
                 : enumerator->GetDevice(deviceId_.c_str(), device.GetAddressOf());
    }
    if (SUCCEEDED(hr))
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(client_.GetAddressOf()));

    WAVEFORMATEX *mix = nullptr;
    if (SUCCEEDED(hr))
        hr = client_->GetMixFormat(&mix);
    if (SUCCEEDED(hr)) {
        SourceFormat format;
        if (!DescribeFormat(*mix, &format) || !converter_.Init(format, gain_))
            hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
    }
    if (SUCCEEDED(hr))
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, loopback_ ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0, kBufferHns,
                                 0, mix, nullptr);
    CoTaskMemFree(mix);
    if (SUCCEEDED(hr))
        hr = client_->GetService(IID_PPV_ARGS(capture_.GetAddressOf()));
    return hr;
}

HRESULT WasapiCapture::Start()
{
    LARGE_INTEGER ticks, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&ticks);
    startTicks_ = ticks.QuadPart;
    ticksPerSecond_ = frequency.QuadPart > 0 ? frequency.QuadPart : 1;

    const HRESULT hr = client_->Start();
    started_ = SUCCEEDED(hr);
    return hr;
}

double WasapiCapture::SecondsRunning() const
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - startTicks_) / static_cast<double>(ticksPerSecond_);
}

HRESULT WasapiCapture::Poll(std::vector<std::int16_t> *frames)
{
    const std::size_t before = frames->size();
    for (;;) {
        UINT32 packetFrames = 0;
        HRESULT hr = capture_->GetNextPacketSize(&packetFrames);
        if (FAILED(hr))
            return hr;
        if (packetFrames == 0)
            break;

        BYTE *data = nullptr;
        UINT32 count = 0;
        DWORD flags = 0;
        hr = capture_->GetBuffer(&data, &count, &flags, nullptr, nullptr);
        if (FAILED(hr))
            return hr;
        if (count == 0)                                             /* AUDCLNT_S_BUFFER_EMPTY */
            break;
        converter_.Convert(data, count, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0, frames);
        hr = capture_->ReleaseBuffer(count);
        if (FAILED(hr))
            return hr;
    }

    if (loopback_) {
        const std::size_t delivered = (frames->size() - before) / kPcmChannels;
        const std::size_t owed = filler_.Owed(SecondsRunning(), delivered);
        frames->insert(frames->end(), owed * kPcmChannels, 0);
    }
    return S_OK;
}

void WasapiCapture::Stop()
{
    if (client_ && started_)
        client_->Stop();
    started_ = false;
    capture_.Reset();
    client_.Reset();
}

void OptOutOfDucking()
{
    /* The preference lives on this process's default session on the default
     * playback device, and has to be set before any stream opens. */
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioSessionManager2> manager;
    ComPtr<IAudioSessionControl> control;
    ComPtr<IAudioSessionControl2> control2;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
        SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf())) &&
        SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr,
                                   reinterpret_cast<void **>(manager.GetAddressOf()))) &&
        SUCCEEDED(manager->GetAudioSessionControl(nullptr, 0, control.GetAddressOf())) &&
        SUCCEEDED(control.As(&control2)))
        control2->SetDuckingPreference(TRUE);
}

}  // namespace audiorgui
