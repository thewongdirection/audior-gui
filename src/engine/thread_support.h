/*
 * thread_support.h - Per-thread setup for the recording threads.
 */
#pragma once

#include <windows.h>

#include <avrt.h>
#include <objbase.h>

namespace audiorgui {

/* The thread joins the multithreaded COM apartment for its lifetime. */
class ComScope {
public:
    ComScope() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope()
    {
        if (SUCCEEDED(hr_))
            CoUninitialize();
    }
    ComScope(const ComScope &) = delete;
    ComScope &operator=(const ComScope &) = delete;

private:
    HRESULT hr_;
};

/* Asks Windows to schedule the thread like an audio thread, so a busy machine
 * does not make it miss the device's buffer. */
class AudioThreadScope {
public:
    AudioThreadScope() : handle_(AvSetMmThreadCharacteristicsW(L"Audio", &index_)) {}
    ~AudioThreadScope()
    {
        if (handle_)
            AvRevertMmThreadCharacteristics(handle_);
    }
    AudioThreadScope(const AudioThreadScope &) = delete;
    AudioThreadScope &operator=(const AudioThreadScope &) = delete;

private:
    DWORD  index_ = 0;
    HANDLE handle_;
};

}  // namespace audiorgui
