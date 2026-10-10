#include "wav_sink.h"

#include <cstring>

#include "pcm.h"

namespace audiorgui {

namespace {

constexpr DWORD kHeaderBytes = 44;

void Put32(std::uint8_t *at, std::uint32_t v) { std::memcpy(at, &v, sizeof(v)); }
void Put16(std::uint8_t *at, std::uint16_t v) { std::memcpy(at, &v, sizeof(v)); }

HRESULT LastError() { return HRESULT_FROM_WIN32(GetLastError()); }

}  // namespace

std::unique_ptr<IAudioSink> WavSink::Create(const std::wstring &path, HRESULT *hr, std::uint64_t maxDataBytes)
{
    /* CREATE_NEW: an existing recording is never overwritten. */
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *hr = LastError();
        return nullptr;
    }
    std::unique_ptr<WavSink> sink(new WavSink(file, path, maxDataBytes));
    *hr = sink->WriteHeader();                  /* a placeholder; the sizes are filled in on Close */
    if (FAILED(*hr)) {
        sink->Close(false);
        return nullptr;
    }
    return sink;
}

/* The 44-byte RIFF header for the data written so far, at the start of the file. */
HRESULT WavSink::WriteHeader()
{
    const std::uint32_t data = static_cast<std::uint32_t>(dataBytes_);
    std::uint8_t h[kHeaderBytes];
    std::memcpy(h + 0, "RIFF", 4);
    Put32(h + 4, 36 + data);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    Put32(h + 16, 16);                          /* size of the fmt chunk */
    Put16(h + 20, 1);                           /* PCM */
    Put16(h + 22, static_cast<std::uint16_t>(kPcmChannels));
    Put32(h + 24, kPcmRate);
    Put32(h + 28, kPcmBytesPerSecond);
    Put16(h + 32, static_cast<std::uint16_t>(kPcmFrameBytes));
    Put16(h + 34, 16);                          /* bits per sample */
    std::memcpy(h + 36, "data", 4);
    Put32(h + 40, data);

    LARGE_INTEGER start = {}, end = {};
    DWORD written = 0;
    if (!SetFilePointerEx(file_, start, nullptr, FILE_BEGIN) ||
        !WriteFile(file_, h, kHeaderBytes, &written, nullptr) || written != kHeaderBytes)
        return LastError();
    if (!SetFilePointerEx(file_, end, nullptr, FILE_END))
        return LastError();
    return S_OK;
}

HRESULT WavSink::Write(const void *pcm, std::size_t bytes)
{
    if (closed_)
        return E_FAIL;
    if (dataBytes_ + bytes > maxDataBytes_)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

    const std::uint8_t *p = static_cast<const std::uint8_t *>(pcm);
    while (bytes > 0) {
        const DWORD chunk = static_cast<DWORD>(bytes < (1u << 20) ? bytes : (1u << 20));
        DWORD written = 0;
        if (!WriteFile(file_, p, chunk, &written, nullptr))
            return LastError();
        dataBytes_ += written;
        if (written != chunk)
            return HRESULT_FROM_WIN32(ERROR_DISK_FULL);
        p += written;
        bytes -= written;
    }
    return S_OK;
}

HRESULT WavSink::Close(bool keep)
{
    if (closed_)
        return closeHr_;
    closed_ = true;
    if (keep)
        closeHr_ = WriteHeader();               /* the real sizes */
    if (!CloseHandle(file_) && SUCCEEDED(closeHr_))
        closeHr_ = LastError();
    file_ = INVALID_HANDLE_VALUE;
    if (!keep)
        DeleteFileW(path_.c_str());
    return closeHr_;
}

}  // namespace audiorgui
