/*
 * main.cpp - Entry point.
 *
 *     audior_gui.exe                      the window, recording with WASAPI
 *     audior_gui.exe --demo               the window with simulated audio (nothing is written)
 *     audior_gui.exe -o talk.mp3 -i ...   audior's options preset the window; --output also starts recording
 *     audior_gui.exe --list-devices       list the audio devices
 *     audior_gui.exe --stop               stop the recording in progress
 *     audior_gui.exe --help               the options
 *     audior_gui.exe --screenshot out.png [--scenario recording] [--size 1120x720]
 *
 * See core/cli.h for the options.
 */
#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

#include <memory>
#include <string>
#include <vector>

#include "console.h"
#include "core/cli.h"
#include "core/recorder_controller.h"
#include "engine/asr_helper.h"
#include "engine/simulated_engine.h"
#include "engine/stop_signal.h"
#include "engine/wasapi_engine.h"
#include "ui/app_window.h"
#include "ui/screenshot.h"

using namespace audiorgui;

namespace {

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

bool FileExists(const std::wstring &path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::wstring CurrentDirectory()
{
    wchar_t buffer[MAX_PATH + 1];
    const DWORD n = GetCurrentDirectoryW(MAX_PATH, buffer);
    return n > 0 && n < MAX_PATH ? std::wstring(buffer, n) : std::wstring();
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

std::vector<std::wstring> Arguments()
{
    std::vector<std::wstring> args;
    int argc = 0;
    if (wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i)                  /* not the program's own name */
            args.push_back(argv[i]);
        LocalFree(argv);
    }
    return args;
}

/* Commands that print something and exit. */
int RunCommand(CliResult::Command command)
{
    switch (command) {
    case CliResult::Command::Help:
        WriteOut(HelpText());
        return 0;
    case CliResult::Command::ListDevices: {
        WasapiEngine engine;
        WriteOut(FormatDeviceList(engine.ListDevices(DeviceKind::Microphone),
                                  engine.ListDevices(DeviceKind::Playback)));
        return 0;
    }
    case CliResult::Command::Stop:
        if (RequestStop()) {
            WriteOut(L"Stop signal sent.\n");
            return 0;
        }
        WriteError(L"No active recording to stop.\n");
        return 1;
    default:
        return 0;
    }
}

/* Picks the device a --mic / --output-device value names; false (with a message) if there is none. */
bool ChooseDevice(const RecorderController &controller, DeviceKind kind, const std::wstring &selector,
                  std::wstring *id)
{
    if (selector.empty() || ResolveDevice(controller.Devices(kind), selector, id))
        return true;
    WriteError(std::wstring(L"Error: no ") + (kind == DeviceKind::Microphone ? L"microphone" : L"playback device") +
               L" matches '" + selector + L"'. Run audior_gui --list-devices to see them.\n");
    return false;
}

int RunWindow(HINSTANCE instance, int showCommand, const CliResult &cli, const RecordingSettings &settings,
              ui::Gfx &gfx)
{
    std::unique_ptr<IRecordingEngine> engine;
    if (cli.demo)
        engine = std::make_unique<SimulatedEngine>(SecondsNow);
    else
        engine = std::make_unique<WasapiEngine>();

    RecorderController controller(*engine, LocalNow, FileExists);
    controller.Settings() = settings;
    if (!controller.CanTranscribe() && (settings.transcribeMicrophone || settings.transcribeSystem))
        WriteError(L"Warning: live transcription needs the helper audior_asr.exe next to this program and a model in " +
                   ModelsDirectory() + L" (see the README); without them only --demo can transcribe.\n");
    if (!ChooseDevice(controller, DeviceKind::Microphone, cli.microphone, &controller.Settings().microphoneId) ||
        !ChooseDevice(controller, DeviceKind::Playback, cli.outputDevice, &controller.Settings().systemDeviceId))
        return 1;

    ui::AppWindow window(gfx, controller);
    if (!window.Create(instance, showCommand)) {
        WriteError(L"The window could not be created.\n");
        return 1;
    }
    if (cli.start)
        window.StartRecording();
    return window.Run();
}

int Run(HINSTANCE instance, int showCommand)
{
    RecordingSettings settings;
    settings.outputDirectory = DefaultOutputDirectory();
    const CliResult cli = ParseCommandLine(Arguments(), CurrentDirectory(), &settings);
    if (!cli.error.empty()) {
        WriteError(L"Error: " + cli.error + L"\nRun audior_gui --help to see the options.\n");
        return 1;
    }
    for (const std::wstring &warning : cli.warnings)
        WriteError(L"Warning: " + warning + L"\n");

    if (cli.command != CliResult::Command::Run)
        return RunCommand(cli.command);

    ui::Gfx gfx;
    if (FAILED(gfx.Init())) {
        WriteError(L"Direct2D could not be started.\n");
        return 1;
    }
    if (!cli.screenshotPath.empty()) {
        ui::ScreenshotOptions options;
        options.path = cli.screenshotPath;
        if (!cli.scenario.empty())
            options.scenario = cli.scenario;
        if (cli.width > 0) {
            options.width = cli.width;
            options.height = cli.height;
        }
        return ui::RunScreenshot(gfx, options);
    }
    return RunWindow(instance, showCommand, cli, settings, gfx);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        WriteError(L"COM could not be initialised.\n");
        return 1;
    }
    const int code = Run(instance, showCommand);
    CoUninitialize();
    return code;
}
