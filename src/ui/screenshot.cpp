#include "screenshot.h"

#include <dxgiformat.h>

#include <memory>

#include "../core/asr_languages.h"
#include "../core/asr_models.h"
#include "../core/recorder_controller.h"
#include "../core/setup_status.h"
#include "../engine/simulated_engine.h"
#include "main_view.h"

namespace audiorgui::ui {

namespace {

/* A start-up set-up that has one thing to say, for the "setup-" scenarios. */
class ScriptedSetup : public ISetupSource {
public:
    explicit ScriptedSetup(SetupStatus status) : status_(std::move(status)) {}
    bool Poll(SetupStatus *out) override
    {
        if (said_)
            return false;
        *out = status_;
        said_ = true;
        return true;
    }
    void Answer(bool) override {}
    void Cancel() override {}
    void Retry() override {}

private:
    SetupStatus status_;
    bool        said_ = false;
};

/* A controller on a simulated engine whose clock only moves when told to. */
struct Scene {
    double             now = 0.0;
    SimulatedEngine    engine;
    RecorderController ctl;
    std::unique_ptr<ScriptedSetup> setup;

    Scene()
        : engine([this] { return now; }),
          ctl(engine, [] { return LocalTime{2026, 10, 10, 15, 30, 45}; })
    {
        ctl.Settings().outputDirectory = L"C:\\Users\\you\\Documents\\Audior-GUI";
    }

    void Run(double seconds)
    {
        const double step = 1.0 / 60.0;
        for (double t = 0.0; t < seconds - 1e-9; t += step) {
            now += step;
            ctl.Tick(step);
        }
    }
};

/* The window while the start-up set-up of transcription has `name` to say: no speech model is installed
 * (except once the download is done), on a PC with a graphics card made up for the picture. */
bool BuildSetup(Scene &scene, const std::wstring &name)
{
    HardwareProfile hardware;
    hardware.vulkanDriver = true;
    hardware.gpus.push_back({L"Demo Graphics Card", 12ull << 30});
    const SetupPlan plan = PlanSetup(hardware, true, {}, false);

    SetupStatus status;
    if (name == L"setup-scan") {
        status = ScanningStatus();
    } else if (name == L"setup-ask") {
        status = AskingStatus(plan);
    } else if (name == L"setup-download") {                 /* the second file, two fifths of the way */
        status = DownloadingStatus(plan, 1, plan.download[0]->bytes + plan.download[1]->bytes * 2 / 5);
    } else if (name == L"setup-done") {                     /* the models have arrived: the Transcript section is complete */
        status = ReadyStatus(plan.gpu, plan.download[0]->label, true);
        status.modelsChanged = true;
    } else if (name == L"setup-failed") {
        status = FailedStatus(L"Download failed", std::wstring(L"The download of ") + plan.download[0]->label +
                                                          L" did not finish: the connection was lost (error 12030).");
        status.canRetry = true;
    } else if (name == L"setup-declined") {                 /* "Not now" was answered before: nothing to show but the link */
        status.scanned = true;
        status.canRetry = true;
    } else {
        return false;
    }
    scene.engine.LackModels();
    scene.setup = std::make_unique<ScriptedSetup>(status);
    scene.ctl.AttachSetup(scene.setup.get());
    scene.ctl.Tick(0.0);                                    /* takes what the set-up says */
    return true;
}

/* Sets the scene up for `name`; false if there is no such scenario. */
bool Build(Scene &scene, const std::wstring &name)
{
    RecorderController &ctl = scene.ctl;
    if (name.compare(0, 6, L"setup-") == 0)
        return BuildSetup(scene, name);
    if (name == L"idle" || name == L"hotkeys" || name == L"hotkey-taken")      /* the view does the rest */
        return true;
    if (name == L"model") {                              /* idle, a Whisper model chosen, transcripts on */
        RecordingSettings &s = ctl.Settings();
        s.includeSystemAudio = s.transcribeMicrophone = s.transcribeSystem = true;
        s.transcribeModel = kAsrModels[1].file;
        return true;
    }
    if (name == L"named" || name == L"typing") {         /* idle, with every setting changed from its default */
        RecordingSettings &s = ctl.Settings();
        s.includeSystemAudio = true;
        s.mixMode = MixMode::Separate;
        s.micAmplifyPercent = 25;
        s.systemAmplifyPercent = 40;
        s.fileName = name == L"named" ? L"team-call" : L"weekly sync with the design team notes";
        return true;
    }

    const bool transcripts = name == L"transcript" || name == L"transcript-saved" || name == L"catching-up" ||
                             name == L"transcript-chinese";
    if (name == L"recording" || name == L"saved" || name == L"error" || name == L"clipping" || transcripts) {
        ctl.Settings().includeSystemAudio = true;
        if (transcripts)
            ctl.Settings().transcribeMicrophone = ctl.Settings().transcribeSystem = true;
        if (name == L"transcript-chinese") {            /* the meeting in Chinese: Whisper, told the language */
            ctl.Settings().transcribeModel = kAsrModels[1].file;
            FindSpeechLanguage(L"zh", &ctl.Settings().transcribeLanguage);
        }
        if (name == L"catching-up")
            scene.engine.FallBehindAfter(8.0);
        if (name == L"clipping")
            ctl.Settings().micAmplifyPercent = 300;
        if (name == L"error")
            scene.engine.FailAfter(4.0, L"Audio capture failed (hr=0x88890004)");
    } else if (name == L"system-only") {
        ctl.Settings().includeMicrophone = false;
        ctl.Settings().includeSystemAudio = true;
    } else if (name == L"bluetooth-mic") {              /* idle, with a Bluetooth headset's microphone chosen */
        ctl.Settings().microphoneId = L"sim-mic-3";
        return true;
    } else if (name != L"mic-only") {
        return false;
    }

    ctl.Start();
    if (name == L"clipping") {
        /* Record until the microphone has just clipped, so the flag is lit. */
        for (int i = 0; i < 3600 && !ctl.Lane(StreamId::Microphone).Clipping(); ++i)
            scene.Run(1.0 / 60.0);
    } else {
        scene.Run(name == L"error" ? 6.0 : transcripts ? 21.0 : 9.0);    /* the transcript: mid-sentence */
    }
    if (name == L"saved" || name == L"transcript-saved")
        ctl.Stop();
    return true;
}

HRESULT Save(const Gfx &gfx, IWICBitmap *bitmap, UINT width, UINT height, const wchar_t *path)
{
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    HRESULT hr = gfx.Wic()->CreateStream(&stream);
    if (SUCCEEDED(hr))
        hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (SUCCEEDED(hr))
        hr = gfx.Wic()->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr))
        hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr))
        hr = frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (SUCCEEDED(hr))
        hr = frame->SetPixelFormat(&format);            /* the encoder converts if it must */
    if (SUCCEEDED(hr))
        hr = frame->WriteSource(bitmap, nullptr);
    if (SUCCEEDED(hr))
        hr = frame->Commit();
    if (SUCCEEDED(hr))
        hr = encoder->Commit();
    return hr;
}

}  // namespace

int RunScreenshot(const Gfx &gfx, const ScreenshotOptions &options)
{
    Scene scene;
    if (!Build(scene, options.scenario))
        return 2;

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(gfx.Wic()->CreateBitmap(options.width, options.height, GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapCacheOnDemand, &bitmap)))
        return 1;

    ComPtr<ID2D1RenderTarget> target;
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (FAILED(gfx.D2D()->CreateWicBitmapRenderTarget(bitmap.Get(), props, &target)))
        return 1;
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    Painter painter(gfx);
    if (FAILED(painter.Bind(target.Get())))
        return 1;

    MainView view(scene.ctl, nullptr);
    if (options.scenario == L"typing")
        view.FocusFileName();
    else if (options.scenario == L"hotkeys")
        view.ListenForHotkey(HotkeyAction::Start);
    else if (options.scenario == L"hotkey-taken")
        view.SetHotkey(HotkeyAction::Stop, kDefaultStopHotkey, false);
    view.Layout(static_cast<float>(options.width), static_cast<float>(options.height));
    target->BeginDraw();
    view.Paint(painter);
    if (FAILED(target->EndDraw()))
        return 1;

    return SUCCEEDED(Save(gfx, bitmap.Get(), options.width, options.height, options.path.c_str())) ? 0 : 1;
}

}  // namespace audiorgui::ui
