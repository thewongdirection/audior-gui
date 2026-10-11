#include "wasapi_engine.h"

#include "devices.h"
#include "mp3_sink.h"
#include "padded_source.h"
#include "wasapi_capture.h"
#include "wav_sink.h"

namespace audiorgui {

void CreateOutputFolder(const std::wstring &filePath)
{
    const std::size_t slash = filePath.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 0)
        CreateDirectoryW(filePath.substr(0, slash).c_str(), nullptr);       /* fails harmlessly if it exists */
}

std::vector<DeviceInfo> WasapiEngine::ListDevices(DeviceKind kind) { return EnumerateDevices(kind); }

bool WasapiEngine::Start(const RecordingSettings &settings, const std::wstring &outputPath, std::wstring *error)
{
    if (IsRecording()) {
        *error = L"A recording is already running.";
        return false;
    }
    if (settings.format == Format::Mp3 && !Mp3Sink::Supported()) {
        *error = L"MP3 recording needs Media Foundation, which this edition of Windows does not include. "
                 L"Choose WAV, or install the Media Feature Pack.";
        return false;
    }

    session_.reset();                                   /* the previous recording's threads and results */
    stopSignal_.Close();
    if (!stopSignal_.Create()) {
        *error = L"The recording could not be started.";
        return false;
    }

    const RecordingSettings s = settings;               /* the factories outlive this call */
    auto sources = [s](StreamId stream) -> std::unique_ptr<ICaptureSource> {
        const bool microphone = stream == StreamId::Microphone;
        auto capture = std::make_unique<WasapiCapture>(microphone ? s.microphoneId : s.systemDeviceId, !microphone,
                                                       GainForPercent(microphone ? s.micAmplifyPercent
                                                                                 : s.systemAmplifyPercent));
        if (microphone)
            return capture;
        return std::make_unique<PaddedSource>(std::move(capture), MonotonicSeconds);   /* idle playback: silence */
    };
    auto sinks = [s](const std::wstring &path, HRESULT *hr) {
        CreateOutputFolder(path);                       /* only now that the devices opened */
        return s.format == Format::Wav ? WavSink::Create(path, hr) : Mp3Sink::Create(path, s.mp3BitrateKbps, hr);
    };

    session_ = std::make_unique<RecordingSession>(sources, sinks, stopSignal_.Handle());
    OptOutOfDucking();
    if (!session_->Start(MakePlan(s, outputPath), error)) {
        session_.reset();
        stopSignal_.Close();
        return false;
    }
    return true;
}

void WasapiEngine::Stop()
{
    if (session_)
        session_->Stop();
    stopSignal_.Close();                                /* `--stop` finds nothing to stop from now on */
}

std::size_t WasapiEngine::TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity)
{
    return session_ ? session_->TakePeaks(stream, out, capacity) : 0;
}

}  // namespace audiorgui
