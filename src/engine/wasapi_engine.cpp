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

WasapiEngine::WasapiEngine() { RescanTranscription(); }

void WasapiEngine::RescanTranscription()
{
    transcribes_ = FindAsr(ProgramDirectory(), ModelsDirectory(), &asr_);
    models_.clear();
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
    for (std::unique_ptr<TranscriptFile> &file : transcriptFiles_)
        file.reset();                                   /* (after host_, which writes to them) */
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
        options.vadModel = asr.vadPath;
        options.language = s.transcribeLanguage;
        options.gpu = s.transcribeGpu;
        options.engine = asr.model->engine;
        for (int i = 0; i < kStreamCount; ++i) {        /* a text file for each transcript, beside the recording */
            if (plan.speech & (1u << i))
                transcriptFiles_[i] = std::make_unique<TranscriptFile>(PlannedTranscriptFile(outputPath, static_cast<StreamId>(i)));
        }
        options.onEvent = [this](StreamId stream, const TranscriptEvent &event) {
            if (TranscriptFile *file = transcriptFiles_[StreamIndex(stream)].get())
                file->Add(event);
        };
        for (const AsrModel *smaller :                  /* what to change to if this model cannot keep up */
             SmallerModels(kAsrModels, asr.model, InstalledModels(ModelsDirectory()))) {
            AsrSetup fallback;
            if (FindAsr(ProgramDirectory(), ModelsDirectory(), &fallback, kAsrModels, kAsrModelCount, smaller->file))
                options.fallbacks.push_back({fallback.modelPath, MakeProcessFactory(fallback), smaller->engine});
        }
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
    for (std::unique_ptr<TranscriptFile> &file : transcriptFiles_) {
        if (file)
            file->Close();                              /* and they are in the transcript files */
    }
    stopSignal_.Close();                                /* `--stop` finds nothing to stop from now on */
}

std::vector<std::wstring> WasapiEngine::TranscriptFiles()
{
    std::vector<std::wstring> paths;
    for (const std::unique_ptr<TranscriptFile> &file : transcriptFiles_) {
        if (file && file->Written())
            paths.push_back(file->Path());
    }
    return paths;
}

std::wstring WasapiEngine::TranscriptFileProblem()
{
    for (const std::unique_ptr<TranscriptFile> &file : transcriptFiles_) {
        if (file && file->Error() != 0)
            return L"transcript not saved (Windows error " + std::to_wstring(file->Error()) + L")";
    }
    return std::wstring();
}

std::size_t WasapiEngine::TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity)
{
    return session_ ? session_->TakePeaks(stream, out, capacity) : 0;
}

}  // namespace audiorgui
