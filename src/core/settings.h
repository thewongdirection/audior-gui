/*
 * settings.h - Recording settings and the rules that go with them.
 *
 * The settings and limits mirror audior's command-line options one for one
 * (see audior's USAGE.md), so the GUI and the CLI accept exactly the same
 * values.
 *
 *   audior option           setting
 *   -f, --format            format
 *   -b, --bitrate           mp3BitrateKbps      96 112 128 160 192 224 256 320
 *   -m, --mic               microphoneId        "" = system default
 *   -a, --amplify           micAmplifyPercent   0..300 (microphone only)
 *   -i, --include-output    includeSystemAudio
 *   -d, --output-device     systemDeviceId      "" = system default
 *   --merge / --separate    mixMode
 *   --merge <percent>       mergeMicPercent     0..100 (the microphone's share)
 *   -o, --output            outputDirectory     (the GUI names the file itself)
 */
#pragma once

#include <string>
#include <vector>

#include "types.h"

namespace audiorgui {

inline constexpr int kMp3Bitrates[] = {96, 112, 128, 160, 192, 224, 256, 320};
inline constexpr int kMp3BitrateCount =
    static_cast<int>(sizeof(kMp3Bitrates) / sizeof(kMp3Bitrates[0]));
inline constexpr int kDefaultMp3Bitrate = 128;
inline constexpr int kMaxAmplifyPercent = 300;
inline constexpr int kDefaultMergeMicPercent = 65;

bool IsValidMp3Bitrate(int kbps);

struct RecordingSettings {
    std::wstring outputDirectory;
    Format       format             = Format::Mp3;
    int          mp3BitrateKbps     = kDefaultMp3Bitrate;
    std::wstring microphoneId;                          /* "" = default */
    int          micAmplifyPercent  = 0;
    bool         includeSystemAudio = false;
    std::wstring systemDeviceId;                        /* "" = default */
    MixMode      mixMode            = MixMode::Merge;
    int          mergeMicPercent    = kDefaultMergeMicPercent;
};

/* An empty string when the settings are valid, else a message for the user. */
std::wstring Validate(const RecordingSettings &settings);

/* ---- output files ---------------------------------------------------- */

struct LocalTime {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

/* "audior-20261010-153045": a file name (no extension) for a recording
 * started at `t`. */
std::wstring DefaultBaseName(const LocalTime &t);

const wchar_t *ExtensionFor(Format format);                 /* L".mp3" / L".wav" */

/* `dir` + `\` + `name`, without doubling a trailing separator. */
std::wstring JoinPath(const std::wstring &dir, const std::wstring &name);

/* Insert `suffix` before the extension: ("a\rec.wav", "_mic") -> "a\rec_mic.wav".
 * This is the rule audior uses for --separate. */
std::wstring SuffixedPath(const std::wstring &path, const std::wstring &suffix);

/* The files a recording to `outputPath` writes: one file, or the "_mic" and
 * "_system" pair when the two streams are kept separate. */
std::vector<std::wstring> PlannedOutputFiles(const RecordingSettings &settings,
                                             const std::wstring &outputPath);

}  // namespace audiorgui
