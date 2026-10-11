#include "devices.h"

#include <windows.h>

#include <objbase.h>
#include <initguid.h>           /* defines the property key below in this file */
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

#include <string>
#include <utility>

#include "../core/bluetooth.h"

namespace audiorgui {

namespace {

using Microsoft::WRL::ComPtr;

std::wstring IdOf(IMMDevice *device)
{
    std::wstring id;
    LPWSTR text = nullptr;
    if (SUCCEEDED(device->GetId(&text)) && text)
        id = text;
    CoTaskMemFree(text);
    return id;
}

/* A text property of the endpoint; empty if it has none. */
std::wstring TextOf(IMMDevice *device, const PROPERTYKEY &key)
{
    std::wstring text;
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, store.GetAddressOf()))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(key, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
            text = value.pwszVal;
        PropVariantClear(&value);
    }
    return text;
}

std::wstring NameOf(IMMDevice *device)
{
    std::wstring name = TextOf(device, PKEY_Device_FriendlyName);
    return name.empty() ? L"Unnamed device" : name;
}

}  // namespace

std::vector<DeviceInfo> EnumerateDevices(DeviceKind kind)
{
    std::vector<DeviceInfo> devices;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        return devices;

    const EDataFlow flow = kind == DeviceKind::Microphone ? eCapture : eRender;

    std::wstring defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, defaultDevice.GetAddressOf())))
        defaultId = IdOf(defaultDevice.Get());

    ComPtr<IMMDeviceCollection> collection;
    UINT count = 0;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.GetAddressOf())) ||
        FAILED(collection->GetCount(&count)))
        return devices;

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, device.GetAddressOf())))
            continue;
        DeviceInfo info;
        info.id = IdOf(device.Get());
        info.name = NameOf(device.Get());
        info.bluetooth = IsBluetoothEnumerator(TextOf(device.Get(), PKEY_Device_EnumeratorName));
        info.isDefault = !defaultId.empty() && info.id == defaultId;
        devices.push_back(std::move(info));
    }
    return devices;
}

}  // namespace audiorgui
