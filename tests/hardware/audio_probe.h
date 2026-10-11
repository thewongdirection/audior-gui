/*
 * audio_probe.h - Reads a WAV or MP3 file the way a listener would: what it is, how long it
 * is and how loud it is over any stretch of it.
 *
 * The audio is streamed through in blocks of a tenth of a second and only each block's peak
 * and energy are kept, so a long file costs a few kilobytes.  MP3 is decoded by Media
 * Foundation, which the caller must have started (MFStartup).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class AudioProbe {
public:
    static constexpr double kSilenceDb = -200.0;

    explicit AudioProbe(const std::wstring &path);

    bool               Valid() const { return valid_; }
    const std::string &Format() const { return format_; }           /* "wav" or "mp3" */
    int                SampleRate() const { return rate_; }
    int                Channels() const { return channels_; }
    double             Seconds() const { return rate_ ? static_cast<double>(frames_) / rate_ : 0.0; }
    double             Kbps() const { return Seconds() > 0 ? fileBytes_ * 8.0 / Seconds() / 1000.0 : 0.0; }
    /* WAV only: the sizes the header states are the sizes of the file. */
    bool               HeaderMatchesData() const { return headerMatches_; }

    /* Peak and RMS level in dB below full scale over [from, from + length) seconds, rounded out to
     * whole blocks.  kSilenceDb for digital silence; NaN if the file does not reach that far. */
    double PeakDb(double from, double length) const;
    double MeanDb(double from, double length) const;

private:
    struct Block {
        float         peak = 0.0f;
        double        energy = 0.0;
        std::uint32_t samples = 0;
    };

    bool ReadWav(void *file, const std::uint8_t *riffHeader);
    bool ReadMp3(const std::wstring &path);
    void Add(const std::int16_t *samples, std::size_t count);
    bool Range(double from, double length, std::size_t *first, std::size_t *last) const;

    bool                valid_ = false;
    std::string         format_;
    int                 rate_ = 0;
    int                 channels_ = 0;
    std::uint64_t       fileBytes_ = 0;
    std::uint64_t       frames_ = 0;
    bool                headerMatches_ = false;
    std::size_t         blockFrames_ = 1;
    std::size_t         blockFill_ = 0;
    std::vector<Block>  blocks_;
};
