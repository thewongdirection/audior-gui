/*
 * types.h - Vocabulary types shared by the core, the engines and the UI.
 *
 * Everything under src/core is plain C++17 with no Windows or UI headers, so
 * the logic can be unit-tested without a window or any audio hardware.
 */
#pragma once

#include <cstdint>
#include <string>

namespace audiorgui {

/* One captured audio stream.  The window shows one waveform lane per stream. */
enum class StreamId { Microphone = 0, System = 1 };
constexpr int kStreamCount = 2;
constexpr int StreamIndex(StreamId s) { return static_cast<int>(s); }

enum class Format     { Mp3, Wav };
enum class MixMode    { Merge, Separate };
enum class DeviceKind { Microphone, Playback };

/* The language of the speech a transcript is made of: found by the model, or English. */
enum class SpeechLanguage { Auto, English };

struct DeviceInfo {
    std::wstring id;          /* endpoint ID; usable as a device selector */
    std::wstring name;        /* friendly name                            */
    bool         isDefault = false;
};

/* The extremes of the normalised (-1..+1) samples over one short slice of
 * audio.  Engines deliver audio to the UI only in this form. */
struct PeakBin {
    float low  = 0.0f;
    float high = 0.0f;
};

/* Engines produce peak bins at this rate, whatever the audio format. */
constexpr int kPeakBinsPerSecond = 100;

struct RecordingStats {
    double        seconds      = 0.0;
    std::uint64_t bytesWritten = 0;
};

}  // namespace audiorgui
