#include "wasapi_engine.h"

#include "devices.h"
#include "mp3_sink.h"
#include "wasapi_capture.h"
#include "wav_sink.h"

namespace audiorgui {

void CreateOutputFolder(const std::wstring &filePath)
{
    const std::size_t slash = filePath.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash > 0)
        CreateDirectoryW(filePath.substr(0, slash).c_str(), nullptr);       /* fails harmlessly if it exists */
}

WasapiEngine::WasapiEngine() : transcribes_(FindAsr(ProgramDirectory(), ModelsDirectory(), &asr_))
{
    if (!transcribes_)
        return;
    for (const AsrModel *m : InstalledModels(ModelsDirectory()))
        models_.push_back(ModelChoice(*m));
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

    AsrSetup asr = asr_;                                /* the model this recording transcribes with */
    if (transcribes_ && !settings.transcribeModel.empty() &&
        (Transcribes(settings, StreamId::Microphone) || Transcribes(settings, StreamId::System)) &&
        !FindAsr(ProgramDirectory(), ModelsDirectory(), &asr, kAsrModels, kAsrModelCount, settings.transcribeModel)) {
        *error = L"The transcription model " + settings.transcribeModel + L" is not in " + ModelsDirectory() + L".";
        return false;                                   /* asked for by name: better said now than not at all */
    }

    session_.reset();                                   /* the previous recording's threads and results */
    host_.reset();
    stopSignal_.Close();
    if (!stopSignal_.Create()) {
        *error = L"The recording could not be started.";
        return false;
    }

    const RecordingSettings s = settings;               /* the factories outlive this call */
    const auto clock = std::make_shared<SessionClock>();    /* one time origin for all the streams */
    auto sources = [s, clock](StreamId stream) -> std::unique_ptr<ICaptureSource> {
        const bool microphone = stream == StreamId::Microphone;
        return std::make_unique<WasapiCapture>(microphone ? s.microphoneId : s.systemDeviceId, !microphone,
                                               GainForPercent(microphone ? s.micAmplifyPercent
                                                                         : s.systemAmplifyPercent),
                                               clock);
    };
    auto sinks = [s](const std::wstring &path, HRESULT *hr) {
        CreateOutputFolder(path);                       /* only now that the devices opened */
        return s.format == Format::Wav ? WavSink::Create(path, hr) : Mp3Sink::Create(path, s.mp3BitrateKbps, hr);
    };

    const SessionPlan plan = MakePlan(s, outputPath);
    if (transcribes_ && plan.speech != 0) {             /* the transcriber is told first, so it misses none of the audio */
        TranscriptionHost::Options options;
        options.model = asr.modelPath;
        options.language = s.transcribeLanguage;
        host_ = std::make_unique<TranscriptionHost>(MakeProcessFactory(asr), options);
        host_->Start(plan.speech);
    }

    session_ = std::make_unique<RecordingSession>(sources, sinks, stopSignal_.Handle(), host_.get());
    OptOutOfDucking();
    if (!session_->Start(plan, error)) {
        session_.reset();
        host_.reset();                                  /* stops it */
        stopSignal_.Close();
        return false;
    }
    return true;
}

void WasapiEngine::Stop()
{
    if (session_)
        session_->Stop();                               /* the captures end first: no more audio for... */
    if (host_)
        host_->Stop();                                  /* ...the transcriber, which says its last words */
    stopSignal_.Close();                                /* `--stop` finds nothing to stop from now on */
}

std::size_t WasapiEngine::TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity)
{
    return session_ ? session_->TakePeaks(stream, out, capacity) : 0;
}

}  // namespace audiorgui
