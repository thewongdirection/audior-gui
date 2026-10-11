/*
 * cli.h - The command line.
 *
 * Audior-GUI accepts audior's options, so a script or shortcut written for
 * audior works here too:
 *
 *   -o, --output <file>        where to record; giving it starts recording at once
 *   -f, --format wav|mp3       -b, --bitrate <kbps>       -i, --include-output
 *   --merge [percent]          --separate                 -m, --mic <id|index|name>
 *   -a, --amplify <0-300>      -d, --output-device <id|index|name>
 *   -l, --list-devices         -s, --stop                 -h, --help
 *
 * and a few of its own:  --system-amplify <0-300>,  --transcribe-mic,
 * --transcribe-system (live transcripts),  --demo (simulated audio).
 * The options only preset the window; everything stays editable there.
 *
 * Parsing is plain C++ over a list of strings, so it is unit-tested.
 */
#pragma once

#include <string>
#include <vector>

#include "settings.h"
#include "types.h"

namespace audiorgui {

struct CliResult {
    enum class Command { Run, Help, ListDevices, Stop };

    Command command = Command::Run;
    bool    demo    = false;                    /* --demo: simulated audio, nothing is written    */
    bool    start   = false;                    /* --output was given: begin recording at once    */

    /* Device choices still to be matched against the machine's devices (see
     * ResolveDevice); empty when not given. */
    std::wstring microphone;
    std::wstring outputDevice;

    /* Developer options: render the window to a PNG instead of showing it. */
    std::wstring screenshotPath;
    std::wstring scenario;
    unsigned     width = 0, height = 0;

    std::wstring              error;            /* non-empty: the arguments are wrong, say so and stop */
    std::vector<std::wstring> warnings;         /* e.g. a value that was clamped                       */
};

/* Parses `args` (without the program name) into `settings`, which holds the
 * defaults and is changed only by the options given.  `workingDirectory` is
 * what a relative --output is relative to. */
CliResult ParseCommandLine(const std::vector<std::wstring> &args, const std::wstring &workingDirectory,
                           RecordingSettings *settings);

/* Finds the device a --mic / --output-device value names: a list index, else
 * (part of) an endpoint ID, else (part of) a friendly name, ignoring case.
 * Returns false if none matches. */
bool ResolveDevice(const std::vector<DeviceInfo> &devices, const std::wstring &selector, std::wstring *id);

/* The text of --list-devices. */
std::wstring FormatDeviceList(const std::vector<DeviceInfo> &microphones, const std::vector<DeviceInfo> &playback);

/* The text of --help. */
std::wstring HelpText();

}  // namespace audiorgui
