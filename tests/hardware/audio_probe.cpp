#include "audio_probe.h"

#include <windows.h>

#include <mferror.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace {

constexpr std::size_t kReadBytes = 64 * 1024;
constexpr double      kBlocksPerSecond = 10.0;

std::uint16_t Le16(const std::uint8_t *p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t Le32(const std::uint8_t *p) { return static_cast<std::uint32_t>(Le16(p) | (Le16(p + 2) << 16)); }

/* Reads exactly `n` bytes unless the file ends first; returns how many it got. */
std::size_t ReadSome(HANDLE file, void *buffer, std::size_t n)
{
    DWORD got = 0;
    if (!ReadFile(file, buffer, static_cast<DWORD>(n), &got, nullptr))
        return 0;
    return got;
}

double ToDb(double amplitude)
{
    return amplitude > 1e-5 ? 20.0 * std::log10(amplitude) : AudioProbe::kSilenceDb;
}

}  // namespace

AudioProbe::AudioProbe(const std::wstring &path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    LARGE_INTEGER size = {};
    GetFileSizeEx(file, &size);
    fileBytes_ = static_cast<std::uint64_t>(size.QuadPart);

    std::uint8_t head[12] = {};
    const std::size_t got = ReadSome(file, head, sizeof head);
    if (got == sizeof head && std::memcmp(head, "RIFF", 4) == 0 && std::memcmp(head + 8, "WAVE", 4) == 0) {
        valid_ = ReadWav(file, head);
        CloseHandle(file);
    } else {
        CloseHandle(file);
        const bool id3 = got >= 3 && std::memcmp(head, "ID3", 3) == 0;
        const bool frameSync = got >= 2 && head[0] == 0xFF && (head[1] & 0xE0) == 0xE0;
        if (id3 || frameSync)
            valid_ = ReadMp3(path);
    }
}

/* A canonical WAV: "fmt " then "data", 16-bit PCM, which is all audior-gui writes. */
bool AudioProbe::ReadWav(void *handle, const std::uint8_t *riffHeader)
{
    HANDLE file = handle;
    format_ = "wav";
    bool haveFormat = false;
    std::uint32_t declaredData = 0;
    std::uint64_t actualData = 0;

    for (;;) {
        std::uint8_t chunk[8];
        if (ReadSome(file, chunk, sizeof chunk) < sizeof chunk)
            break;
        const std::uint32_t length = Le32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && length >= 16) {
            std::uint8_t fmt[16];
            if (ReadSome(file, fmt, sizeof fmt) < sizeof fmt)
                return false;
            if (Le16(fmt) != 1 || Le16(fmt + 14) != 16)             /* PCM, 16 bits */
                return false;
            channels_ = Le16(fmt + 2);
            rate_ = static_cast<int>(Le32(fmt + 4));
            haveFormat = channels_ > 0 && rate_ > 0;
            LARGE_INTEGER skip;
            skip.QuadPart = (length - 16) + (length & 1);
            SetFilePointerEx(file, skip, nullptr, FILE_CURRENT);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!haveFormat)
                return false;
            blockFrames_ = static_cast<std::size_t>(rate_ / kBlocksPerSecond);
            declaredData = length;
            const std::size_t frameBytes = static_cast<std::size_t>(channels_) * sizeof(std::int16_t);
            std::vector<std::uint8_t> buffer(kReadBytes / frameBytes * frameBytes);
            std::uint64_t left = length;
            while (left > 0) {
                const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(left, buffer.size()));
                const std::size_t n = ReadSome(file, buffer.data(), want) / frameBytes * frameBytes;
                if (n == 0)
                    break;
                Add(reinterpret_cast<const std::int16_t *>(buffer.data()), n / sizeof(std::int16_t));
                actualData += n;
                left -= n;
            }
            break;
        } else {
            LARGE_INTEGER skip;
            skip.QuadPart = static_cast<LONGLONG>(length) + (length & 1);
            SetFilePointerEx(file, skip, nullptr, FILE_CURRENT);
        }
    }
    headerMatches_ = actualData == declaredData && static_cast<std::uint64_t>(Le32(riffHeader + 4)) + 8 == fileBytes_;
    return haveFormat && frames_ > 0;
}

/* Decodes with Media Foundation's source reader, which picks the MP3 decoder itself. */
bool AudioProbe::ReadMp3(const std::wstring &path)
{
    format_ = "mp3";
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader)))
        return false;
    const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    ComPtr<IMFMediaType> wanted;
    if (FAILED(MFCreateMediaType(&wanted)) || FAILED(wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) || FAILED(reader->SetCurrentMediaType(stream, nullptr, wanted.Get())))
        return false;

    ComPtr<IMFMediaType> actual;
    UINT32 bits = 0, channels = 0, rate = 0;
    if (FAILED(reader->GetCurrentMediaType(stream, &actual)) ||
        FAILED(actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits)) ||
        FAILED(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels)) ||
        FAILED(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate)) || bits != 16 || channels == 0 || rate == 0)
        return false;
    channels_ = static_cast<int>(channels);
    rate_ = static_cast<int>(rate);
    blockFrames_ = static_cast<std::size_t>(rate_ / kBlocksPerSecond);

    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(stream, 0, nullptr, &flags, nullptr, &sample)) || (flags & MF_SOURCE_READERF_ERROR))
            return false;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
        ComPtr<IMFMediaBuffer> buffer;
        BYTE *data = nullptr;
        DWORD length = 0;
        if (!sample || FAILED(sample->ConvertToContiguousBuffer(&buffer)) || FAILED(buffer->Lock(&data, nullptr, &length)))
            continue;
        Add(reinterpret_cast<const std::int16_t *>(data), length / sizeof(std::int16_t) / channels * channels);
        buffer->Unlock();
    }
    return frames_ > 0;
}

/* Interleaved 16-bit samples; a whole number of frames. */
void AudioProbe::Add(const std::int16_t *samples, std::size_t count)
{
    const std::size_t channels = static_cast<std::size_t>(channels_);
    for (std::size_t i = 0; i + channels <= count; i += channels) {
        if (blockFill_ == 0)
            blocks_.emplace_back();
        Block &block = blocks_.back();
        for (std::size_t c = 0; c < channels; ++c) {
            const float v = std::fabs(samples[i + c] / 32768.0f);
            block.peak = std::max(block.peak, v);
            block.energy += static_cast<double>(v) * v;
            ++block.samples;
        }
        ++frames_;
        if (++blockFill_ == blockFrames_)
            blockFill_ = 0;
    }
}

bool AudioProbe::Range(double from, double length, std::size_t *first, std::size_t *last) const
{
    const double begin = std::floor(from * kBlocksPerSecond + 1e-9);
    const double end = std::ceil((from + length) * kBlocksPerSecond - 1e-9);
    if (begin < 0 || end > static_cast<double>(blocks_.size()) || end <= begin)
        return false;
    *first = static_cast<std::size_t>(begin);
    *last = static_cast<std::size_t>(end);
    return true;
}

double AudioProbe::PeakDb(double from, double length) const
{
    std::size_t first, last;
    if (!Range(from, length, &first, &last))
        return std::numeric_limits<double>::quiet_NaN();
    float peak = 0.0f;
    for (std::size_t i = first; i < last; ++i)
        peak = std::max(peak, blocks_[i].peak);
    return ToDb(peak);
}

double AudioProbe::MeanDb(double from, double length) const
{
    std::size_t first, last;
    if (!Range(from, length, &first, &last))
        return std::numeric_limits<double>::quiet_NaN();
    double energy = 0.0;
    std::uint64_t samples = 0;
    for (std::size_t i = first; i < last; ++i) {
        energy += blocks_[i].energy;
        samples += blocks_[i].samples;
    }
    return samples ? ToDb(std::sqrt(energy / static_cast<double>(samples))) : kSilenceDb;
}
