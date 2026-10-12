#include "pcm_reader.h"

#include <algorithm>
#include <cwctype>
#include <cstring>

#include "mp3_reader.h"
#include "pcm.h"

namespace audiorgui {

namespace {

constexpr DWORD kMaxChunkBytes = 1u << 20;      /* one ReadFile at most */

HRESULT LastError() { return HRESULT_FROM_WIN32(GetLastError()); }
HRESULT BadFormat() { return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT); }

std::uint16_t Get16(const std::uint8_t *at) { std::uint16_t v; std::memcpy(&v, at, sizeof(v)); return v; }
std::uint32_t Get32(const std::uint8_t *at) { std::uint32_t v; std::memcpy(&v, at, sizeof(v)); return v; }

bool ReadExactly(HANDLE file, void *into, DWORD bytes)
{
    DWORD got = 0;
    return ReadFile(file, into, bytes, &got, nullptr) && got == bytes;
}

bool Skip(HANDLE file, std::uint64_t bytes)
{
    LARGE_INTEGER by = {};
    by.QuadPart = static_cast<LONGLONG>(bytes);
    return SetFilePointerEx(file, by, nullptr, FILE_CURRENT) != 0;
}

class WavReader final : public IPcmReader {
public:
    WavReader(HANDLE file, std::uint64_t frames) : file_(file), total_(frames), remaining_(frames) {}
    ~WavReader() override { CloseHandle(file_); }

    HRESULT Read(std::int16_t *out, std::size_t maxFrames, std::size_t *frames) override
    {
        const std::size_t want = static_cast<std::size_t>(
            std::min<std::uint64_t>({maxFrames, remaining_, kMaxChunkBytes / kPcmFrameBytes}));
        DWORD got = 0;
        *frames = 0;
        if (want > 0 && !ReadFile(file_, out, static_cast<DWORD>(want * kPcmFrameBytes), &got, nullptr))
            return LastError();
        *frames = got / kPcmFrameBytes;
        remaining_ = *frames < want ? 0 : remaining_ - *frames;         /* a file cut short ends early */
        return S_OK;
    }

    std::uint64_t TotalFrames() const override { return total_; }

private:
    HANDLE        file_;
    std::uint64_t total_, remaining_;
};

}  // namespace

/* The chunks of a RIFF file: "fmt " says what the audio is, "data" holds it.  Anything else is stepped over. */
std::unique_ptr<IPcmReader> OpenWavReader(const std::wstring &path, HRESULT *hr)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *hr = LastError();
        return nullptr;
    }
    LARGE_INTEGER size = {};
    std::uint8_t header[12];
    *hr = GetFileSizeEx(file, &size) ? S_OK : LastError();
    if (SUCCEEDED(*hr) && (!ReadExactly(file, header, sizeof(header)) || std::memcmp(header, "RIFF", 4) != 0 ||
                           std::memcmp(header + 8, "WAVE", 4) != 0))
        *hr = BadFormat();

    bool formatOk = false;
    while (SUCCEEDED(*hr)) {
        std::uint8_t chunk[8];
        if (!ReadExactly(file, chunk, sizeof(chunk))) {
            *hr = BadFormat();                                  /* the file ends before its data */
            break;
        }
        const std::uint32_t length = Get32(chunk + 4);
        const std::uint64_t padded = static_cast<std::uint64_t>(length) + (length & 1);      /* (not 32 bits: it must not wrap) */
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            std::uint8_t fmt[40];
            if (length < 16 || length > sizeof(fmt) || !ReadExactly(file, fmt, length) || !Skip(file, padded - length) ||
                Get16(fmt) != 1 || Get16(fmt + 2) != kPcmChannels || Get32(fmt + 4) != kPcmRate || Get16(fmt + 14) != 16) {
                *hr = BadFormat();                              /* only the recorder's own format is read */
                break;
            }
            formatOk = true;
        } else if (std::memcmp(chunk, "data", 4) == 0 && formatOk) {
            LARGE_INTEGER at = {}, none = {};
            if (!SetFilePointerEx(file, none, &at, FILE_CURRENT)) {
                *hr = LastError();
                break;
            }
            const std::uint64_t bytes = std::min<std::uint64_t>(length, static_cast<std::uint64_t>(size.QuadPart - at.QuadPart));
            return std::make_unique<WavReader>(file, bytes / kPcmFrameBytes);
        } else if (!Skip(file, padded)) {
            *hr = LastError();
        }
    }
    CloseHandle(file);
    return nullptr;
}

std::unique_ptr<IPcmReader> OpenPcmReader(const std::wstring &path, HRESULT *hr)
{
    const std::size_t dot = path.rfind(L'.');
    std::wstring extension = dot == std::wstring::npos ? std::wstring() : path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (extension == L".wav")
        return OpenWavReader(path, hr);
    if (extension == L".mp3")
        return OpenMp3Reader(path, hr);
    *hr = HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
    return nullptr;
}

}  // namespace audiorgui
