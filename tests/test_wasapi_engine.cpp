/* Tests of the WASAPI engine that need no audio hardware: they exercise the
 * paths a machine without a microphone (or a CI runner) takes. */
#include <string>

#include "../src/engine/stop_signal.h"
#include "../src/engine/wasapi_engine.h"
#include "temp_dir.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

/* An endpoint ID that no machine has. */
const wchar_t kNoSuchDevice[] = L"{0.0.1.00000000}.{00000000-0000-0000-0000-00000000dead}";

RecordingSettings WavSettings(const TempDir &dir)
{
    RecordingSettings s;
    s.outputDirectory = dir.Dir();
    s.format = Format::Wav;
    return s;
}

}  // namespace

TEST(GainForPercent_ScalesFromUnity)
{
    CHECK_NEAR(GainForPercent(0), 1.0, 1e-6);
    CHECK_NEAR(GainForPercent(100), 2.0, 1e-6);
    CHECK_NEAR(GainForPercent(300), 4.0, 1e-6);
}

TEST(CreateOutputFolder_MakesTheLastFolderOnly)
{
    TempDir dir;
    CreateOutputFolder(dir.Path(L"recordings\\a.wav"));
    const DWORD made = GetFileAttributesW(dir.Path(L"recordings").c_str());
    CHECK(made != INVALID_FILE_ATTRIBUTES && (made & FILE_ATTRIBUTE_DIRECTORY));

    CreateOutputFolder(dir.Path(L"recordings\\b.wav"));          /* already there: fine */
    CreateOutputFolder(dir.Path(L"x\\y\\c.wav"));                /* its parent is missing too */
    CHECK_EQ(GetFileAttributesW(dir.Path(L"x").c_str()), INVALID_FILE_ATTRIBUTES);
    CreateOutputFolder(L"a.wav");                                   /* no folder at all */
}

TEST(WasapiEngine_IsTheRealEngine)
{
    WasapiEngine engine;
    CHECK(!engine.IsSimulated());
    CHECK(!engine.IsRecording());
    CHECK(engine.OutputFiles().empty());
    CHECK(engine.Failure().empty());
    PeakBin bin[4];
    CHECK_EQ(engine.TakePeaks(StreamId::Microphone, bin, 4), std::size_t(0));
}

TEST(WasapiEngine_ListsDevicesConsistently)
{
    WasapiEngine engine;                                /* a machine without audio lists none */
    for (DeviceKind kind : {DeviceKind::Microphone, DeviceKind::Playback}) {
        int defaults = 0;
        for (const DeviceInfo &device : engine.ListDevices(kind)) {
            CHECK(!device.id.empty());
            CHECK(!device.name.empty());
            defaults += device.isDefault ? 1 : 0;
        }
        CHECK(defaults <= 1);
    }
}

TEST(WasapiEngine_AMissingMicrophoneFailsCleanly)
{
    TempDir dir;
    RecordingSettings s = WavSettings(dir);
    s.microphoneId = kNoSuchDevice;

    WasapiEngine engine;
    std::wstring error;
    CHECK(!engine.Start(s, dir.Path(L"a.wav"), &error));
    CHECK(error.find(L"microphone") != std::wstring::npos);
    CHECK(!engine.IsRecording());
    CHECK(dir.Names().empty());                         /* no empty file left behind */
    CHECK(engine.OutputFiles().empty());
    CHECK(!RequestStop());                              /* and no stop signal is left lying around */

    engine.Stop();                                      /* harmless */
    CHECK(!engine.IsRecording());
}

TEST(WasapiEngine_AMissingPlaybackDeviceFailsCleanly)
{
    TempDir dir;
    RecordingSettings s = WavSettings(dir);
    s.includeMicrophone = false;
    s.includeSystemAudio = true;
    s.systemDeviceId = kNoSuchDevice;

    WasapiEngine engine;
    std::wstring error;
    CHECK(!engine.Start(s, dir.Path(L"a.wav"), &error));
    CHECK(error.find(L"playback device") != std::wstring::npos);
    CHECK(dir.Names().empty());
}

TEST(WasapiEngine_CanTryAgainAfterAFailedStart)
{
    TempDir dir;
    RecordingSettings s = WavSettings(dir);
    s.microphoneId = kNoSuchDevice;

    WasapiEngine engine;
    std::wstring first, second;
    CHECK(!engine.Start(s, dir.Path(L"a.wav"), &first));
    CHECK(!engine.Start(s, dir.Path(L"a.wav"), &second));
    CHECK(!first.empty());
    CHECK_EQ(first, second);
}
