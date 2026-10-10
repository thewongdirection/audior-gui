/*
 * hresult.h - Error text for the recording engine.
 */
#pragma once

#include <cwchar>
#include <string>

namespace audiorgui {

/* "<what> (hr=0x80070070)"; for the failures a user can act on, the reason is
 * added too: "<what>: the disk is full (hr=0x80070070)". */
inline std::wstring WithHr(const wchar_t *what, long hr)
{
    const unsigned long code = static_cast<unsigned long>(hr);
    const wchar_t *reason = nullptr;
    switch (code) {
    case 0x88890004UL: reason = L"the device was unplugged or switched off"; break;   /* AUDCLNT_E_DEVICE_INVALIDATED */
    case 0x8889000AUL: reason = L"another program is using the device exclusively"; break;  /* AUDCLNT_E_DEVICE_IN_USE */
    case 0x80070005UL: reason = L"access was denied (check Windows privacy settings)"; break;  /* E_ACCESSDENIED */
    case 0x80070070UL: reason = L"the disk is full"; break;                            /* ERROR_DISK_FULL */
    case 0x80070003UL: reason = L"the folder does not exist"; break;                   /* ERROR_PATH_NOT_FOUND */
    case 0x80070020UL: reason = L"the file is in use by another program"; break;       /* ERROR_SHARING_VIOLATION */
    case 0x80070050UL: reason = L"the file already exists"; break;                     /* ERROR_FILE_EXISTS */
    case 0x8007007BUL: reason = L"the file name is not valid"; break;                  /* ERROR_INVALID_NAME */
    case 0x800700DFUL: reason = L"WAV files are limited to 4 GiB"; break;              /* ERROR_FILE_TOO_LARGE */
    case 0x80070490UL: reason = L"the device was not found"; break;                    /* E_NOTFOUND */
    default: break;
    }

    wchar_t number[24];
    std::swprintf(number, sizeof(number) / sizeof(number[0]), L"0x%08lX", code);
    std::wstring text = what;
    if (reason) {
        text += L": ";
        text += reason;
    }
    return text + L" (hr=" + number + L")";
}

}  // namespace audiorgui
