/* Tests of the Bluetooth device logic (see core/bluetooth.h). */
#include <vector>

#include "../src/core/bluetooth.h"
#include "../src/engine/simulated_engine.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

DeviceInfo Device(const wchar_t *id, bool isDefault, bool bluetooth)
{
    DeviceInfo d;
    d.id = id;
    d.name = id;
    d.isDefault = isDefault;
    d.bluetooth = bluetooth;
    return d;
}

}  // namespace

TEST(Bluetooth_RecognisesTheBuses)
{
    CHECK(IsBluetoothEnumerator(L"BTHENUM"));             /* a headset's playback side              */
    CHECK(IsBluetoothEnumerator(L"BTHHFENUM"));           /* its hands-free (microphone) side       */
    CHECK(IsBluetoothEnumerator(L"BTHLEDEVICE"));         /* LE Audio                               */
    CHECK(IsBluetoothEnumerator(L"bthenum"));             /* whatever the case                      */
    CHECK(!IsBluetoothEnumerator(L"HDAUDIO"));            /* the PC's own audio, and its monitor    */
    CHECK(!IsBluetoothEnumerator(L"USB"));
    CHECK(!IsBluetoothEnumerator(L"MMDEVAPI"));
    CHECK(!IsBluetoothEnumerator(L"BT"));                 /* too short to be one                    */
    CHECK(!IsBluetoothEnumerator(L""));                   /* a device that names no bus             */
}

TEST(Bluetooth_TheDefaultMicrophoneIsTheOneThatCounts)
{
    const std::vector<DeviceInfo> mics = {Device(L"usb", false, false), Device(L"headset", true, true)};
    CHECK(MicrophoneIsBluetooth(mics, std::wstring()));                 /* "default" resolves to the headset */

    const std::vector<DeviceInfo> pcDefault = {Device(L"usb", true, false), Device(L"headset", false, true)};
    CHECK(!MicrophoneIsBluetooth(pcDefault, std::wstring()));           /* the default is not Bluetooth      */
}

TEST(Bluetooth_AChosenMicrophoneOverridesTheDefault)
{
    const std::vector<DeviceInfo> mics = {Device(L"usb", true, false), Device(L"headset", false, true)};
    CHECK(MicrophoneIsBluetooth(mics, L"headset"));
    CHECK(!MicrophoneIsBluetooth(mics, L"usb"));
}

TEST(Bluetooth_NothingToWarnAboutWithoutADevice)
{
    CHECK(!MicrophoneIsBluetooth({}, std::wstring()));                                      /* no microphone at all */
    CHECK(!MicrophoneIsBluetooth({Device(L"headset", true, true)}, L"gone"));               /* chosen one unplugged */
}

TEST(Bluetooth_TheDemoHasAHeadsetMicrophoneToShowTheNote)
{
    double now = 0.0;
    SimulatedEngine engine([&now] { return now; }, 1);
    const std::vector<DeviceInfo> mics = engine.ListDevices(DeviceKind::Microphone);
    CHECK(!MicrophoneIsBluetooth(mics, std::wstring()));                /* the default demo mic is the PC's  */
    CHECK(MicrophoneIsBluetooth(mics, L"sim-mic-3"));
    CHECK(!MicrophoneIsBluetooth(engine.ListDevices(DeviceKind::Playback), std::wstring()));
}
