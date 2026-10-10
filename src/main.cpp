/*
 * main.cpp - Entry point.
 *
 *     audior_gui.exe                                  run the application
 *     audior_gui.exe --screenshot out.png [--scenario recording] [--size 1120x720]
 *
 * The recording engine is the simulation for now (see engine/simulated_engine.h),
 * so Start records nothing; the window says so.
 */
#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

#include <cwchar>
#include <string>

#include "core/recorder_controller.h"
#include "engine/simulated_engine.h"
#include "ui/app_window.h"
#include "ui/screenshot.h"

using namespace audiorgui;

namespace {

void ShowError(const wchar_t *message)
{
    MessageBoxW(nullptr, message, L"audior", MB_OK | MB_ICONERROR);
}

double SecondsNow()
{
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
}

LocalTime LocalNow()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    return LocalTime{t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond};
}

/* Documents\audior: where recordings go until the user picks a folder. */
std::wstring DefaultOutputDirectory()
{
    PWSTR documents = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documents)))
        dir = std::wstring(documents) + L"\\audior";
    CoTaskMemFree(documents);
    return dir;
}

/* The value following `name` on the command line, or nullptr. */
const wchar_t *OptionValue(int argc, wchar_t **argv, const wchar_t *name)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (_wcsicmp(argv[i], name) == 0)
            return argv[i + 1];
    }
    return nullptr;
}

int Run(HINSTANCE instance, int showCommand)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        ShowError(L"The command line could not be read.");
        return 1;
    }

    ui::Gfx gfx;
    if (FAILED(gfx.Init())) {
        ShowError(L"Direct2D could not be started.");
        LocalFree(argv);
        return 1;
    }

    int code;
    if (const wchar_t *path = OptionValue(argc, argv, L"--screenshot")) {
        ui::ScreenshotOptions options;
        options.path = path;
        if (const wchar_t *scenario = OptionValue(argc, argv, L"--scenario"))
            options.scenario = scenario;
        if (const wchar_t *size = OptionValue(argc, argv, L"--size")) {
            unsigned w = 0, h = 0;
            if (swscanf_s(size, L"%ux%u", &w, &h) == 2 && w >= 200 && w <= 8192 && h >= 200 && h <= 8192) {
                options.width = w;
                options.height = h;
            }
        }
        code = ui::RunScreenshot(gfx, options);
    } else {
        SimulatedEngine engine(SecondsNow);
        RecorderController controller(engine, LocalNow);
        controller.Settings().outputDirectory = DefaultOutputDirectory();

        ui::AppWindow window(gfx, controller);
        if (!window.Create(instance, showCommand)) {
            ShowError(L"The window could not be created.");
            code = 1;
        } else {
            code = window.Run();
        }
    }
    LocalFree(argv);
    return code;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        ShowError(L"COM could not be initialised.");
        return 1;
    }
    const int code = Run(instance, showCommand);
    CoUninitialize();
    return code;
}
