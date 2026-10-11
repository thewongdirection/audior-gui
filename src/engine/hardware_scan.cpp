#include "hardware_scan.h"

#include <windows.h>

#include <dxgi.h>
#include <wrl/client.h>

namespace audiorgui {

namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kMicrosoftVendor = 0x1414;           /* the Basic Render Driver, which older Windows does not flag as software */

/* Whether the PC has the Vulkan loader the graphics driver installs.  The helper is a 64-bit program, so a
 * 32-bit recorder on 64-bit Windows looks where a 64-bit program would (through "Sysnative"). */
bool VulkanLoaderInstalled()
{
    BOOL wow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &wow64);

    wchar_t dir[MAX_PATH];
    const UINT n = wow64 ? GetWindowsDirectoryW(dir, MAX_PATH) : GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return false;
    const std::wstring path = std::wstring(dir, n) + (wow64 ? L"\\Sysnative" : L"") + L"\\vulkan-1.dll";
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

HardwareProfile ScanHardware()
{
    HardwareProfile hw;
    hw.vulkanDriver = VulkanLoaderInstalled();

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return hw;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf())); ++i) {      /* ends at NOT_FOUND, or at an error */
        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ||
            desc.VendorId == kMicrosoftVendor)
            continue;

        GpuInfo gpu;
        gpu.name = desc.Description;
        while (!gpu.name.empty() && gpu.name.back() == L' ')
            gpu.name.pop_back();
        gpu.memoryBytes = desc.DedicatedVideoMemory;
        if (desc.DedicatedVideoMemory == static_cast<SIZE_T>(-1))
            gpu.memoryBytes = 4ull << 30;               /* a 32-bit program is told "4 GiB less a byte" at most: it means that or more */
        if (!gpu.name.empty())
            hw.gpus.push_back(gpu);
    }
    return hw;
}

}  // namespace audiorgui
