#include "screenshot.h"

#include <dxgiformat.h>

#include "../core/recorder_controller.h"
#include "../engine/simulated_engine.h"
#include "main_view.h"

namespace audiorgui::ui {

namespace {

/* A controller on a simulated engine whose clock only moves when told to. */
struct Scene {
    double             now = 0.0;
    SimulatedEngine    engine;
    RecorderController ctl;

    Scene()
        : engine([this] { return now; }),
          ctl(engine, [] { return LocalTime{2026, 10, 10, 15, 30, 45}; })
    {
        ctl.Settings().outputDirectory = L"C:\\Users\\you\\Documents\\audior";
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

/* Sets the scene up for `name`; false if there is no such scenario. */
bool Build(Scene &scene, const std::wstring &name)
{
    RecorderController &ctl = scene.ctl;
    if (name == L"idle")
        return true;

    if (name == L"recording" || name == L"saved" || name == L"error" || name == L"clipping") {
        ctl.Settings().includeSystemAudio = true;
        if (name == L"clipping")
            ctl.Settings().micAmplifyPercent = 300;
        if (name == L"error")
            scene.engine.FailAfter(4.0, L"Audio capture failed (hr=0x88890004)");
    } else if (name != L"mic-only") {
        return false;
    }

    ctl.Start();
    if (name == L"clipping") {
        /* Record until the microphone has just clipped, so the flag is lit. */
        for (int i = 0; i < 3600 && !ctl.Lane(StreamId::Microphone).Clipping(); ++i)
            scene.Run(1.0 / 60.0);
    } else {
        scene.Run(name == L"error" ? 6.0 : 9.0);
    }
    if (name == L"saved")
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
    view.Layout(static_cast<float>(options.width), static_cast<float>(options.height));
    target->BeginDraw();
    view.Paint(painter);
    if (FAILED(target->EndDraw()))
        return 1;

    return SUCCEEDED(Save(gfx, bitmap.Get(), options.width, options.height, options.path.c_str())) ? 0 : 1;
}

}  // namespace audiorgui::ui
