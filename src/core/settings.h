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
 *   (always on in audior)   includeMicrophone   the GUI lets either stream be
 *                                               switched off; one must stay on
 *   -m, --mic               microphoneId        "" = system default
 *   -a, --amplify           micAmplifyPercent   0..300
 *   (not in audior)         systemAmplifyPercent 0..300: the same for system
 *                                               audio, which audior records at
 *                                               unity
 *   -i, --include-output    includeSystemAudio
 *   -d, --output-device     systemDeviceId      "" = system default
 *   --merge / --separate    mixMode
 *   --merge <percent>       mergeMicPercent     0..100 (the microphone's share)
 *   -o, --output            outputDirectory + fileName   (the folder is picked;
 *                           an empty name means a timestamped one)
 *   (not in audior)         transcribeMicrophone / transcribeSystem: a live
 *                           transcript of that stream, for engines that can
 *   --transcribe-mic / --transcribe-system   make one (see IRecordingEngine)
 */
#pragma once

#include <cstddef>
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

/* The longest path a recording may be written to (Windows' MAX_PATH less its
 * terminator).  audior allows 247 characters for the name it is given, leaving
 * room for the extension and the "_system" suffix it then adds. */
inline constexpr std::size_t kMaxOutputPathChars = 259;

bool IsValidMp3Bitrate(int kbps);

struct RecordingSettings {
    std::wstring outputDirectory;
    std::wstring fileName;                              /* "" = a timestamped name */
    Format       format             = Format::Mp3;
    int          mp3BitrateKbps     = kDefaultMp3Bitrate;
    bool         includeMicrophone  = true;
    std::wstring microphoneId;                          /* "" = default */
    int          micAmplifyPercent  = 0;
    bool         includeSystemAudio = false;
    std::wstring systemDeviceId;                        /* "" = default */
    int          systemAmplifyPercent = 0;
    MixMode      mixMode            = MixMode::Merge;
    int          mergeMicPercent    = kDefaultMergeMicPercent;
    bool         transcribeMicrophone = false;
    bool         transcribeSystem   = false;
};

/* True when both streams are recorded: only then do merging and separate
 * tracks mean anything. */
inline bool RecordsBoth(const RecordingSettings &s) { return s.includeMicrophone && s.includeSystemAudio; }

/* True when `stream` is recorded and its live transcript is switched on. */
inline bool Transcribes(const RecordingSettings &s, StreamId stream)
{
    return stream == StreamId::Microphone ? s.includeMicrophone && s.transcribeMicrophone
                                          : s.includeSystemAudio && s.transcribeSystem;
}

/* Record only what the PC plays: the microphone off, the system audio on. */
inline void UseSystemAudioOnly(RecordingSettings *s)
{
    s->includeMicrophone = false;
    s->includeSystemAudio = true;
}

/* An empty string when the settings are valid, else a message for the user. */
std::wstring Validate(const RecordingSettings &settings);

/* ---- the output file's name ------------------------------------------- */

/* Gives `name` the extension of `format`: "meeting" -> "meeting.mp3",
 * "clip.wav" with MP3 -> "clip.mp3", "clip.MP3" stays.  Only the part after the
 * last dot is taken for an extension, so "take.2" becomes "take.mp3".  This is
 * audior's rule for --output. */
std::wstring EnforceExtension(const std::wstring &name, Format format);

/* Why `name` cannot be a file name on Windows (illegal characters, reserved
 * device names such as CON or NUL, a name that is only an extension), or empty
 * if it can.  Also true of the empty name, which stands for a timestamped one. */
std::wstring FileNameProblem(const std::wstring &name);

/* Characters a file name may not contain. */
bool IsFileNameChar(wchar_t c);

/* ---- output files ---------------------------------------------------- */

struct LocalTime {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

/* "Audior-GUI-20261010-153045": a file name (no extension) for a recording
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
