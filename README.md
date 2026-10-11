# audior-gui

A native Windows 10 / 11 recorder with a clean, modern UI: **one live waveform
for every audio stream being recorded**, big **Start** and **Stop** buttons, and
**every option of the `audior` command-line recorder** in a side panel.

> **Status: first hardware results are in.** The WASAPI recording engine is covered by
> tests that run on generated audio, and `hardware_tests.exe` has now recorded with the
> real application on a real PC (100 checks): microphone and system audio, MP3 and WAV,
> gains, device selection, long-recording stability. The one engine problem it found, a
> system-audio track that started earlier than the microphone track of a Bluetooth
> headset, is fixed and re-checked on the hardware (see
> [docs/HARDWARE-TESTS.md](docs/HARDWARE-TESTS.md)). To try it yourself follow
> [docs/FIRST-TEST.md](docs/FIRST-TEST.md). `--demo` runs the window with simulated
> audio and needs no devices.

```
+----------------------------------------------------------------+
|  audior                                                * Ready  |
+------------------------------------------+---------------------+
|  [x] Microphone                  -12 dB  |  SAVE TO            |
|  [Realtek Mic Array v]   Gain --o-- +0%  |  [ ...\audior      v]|
|  |||  ||||||  ||| ||||||||| |||||| |||   |  FILE NAME          |
+------------------------------------------+  [team-call       ] |
|  [x] System audio                -20 dB  |  Saved as team-call.mp3
|  [Speakers (Realtek) v]  Gain --o-- +0%  |  FORMAT             |
|  ||| |||||  |||||| ||||  |||||| ||| ||   |  [MP3|WAV] [128 kbps]|
|                                          |  OUTPUT             |
|                                          |  [merged|2 tracks]  |
+------------------------------------------+---------------------+
|  00:00:12   Recording              [ Stop ] [ * Start recording ]|
+----------------------------------------------------------------+
```

## Requirements

| | |
|---|---|
| Runs on | Windows 10 and Windows 11, x64 or x86 |
| Needs | Nothing to install: no .NET, no Visual C++ redistributable, no third-party library |
| Builds with | Visual Studio 2022 or its Build Tools (C++ workload) |

Like audior, the app uses **only WASAPI (for audio) and libraries that ship with
Windows**: Media Foundation for MP3, Win32 with Direct2D, DirectWrite and WIC for
the window. `tests/check_imports.ps1` fails the build if the executable ever
imports a DLL outside an allowlist of in-box Windows DLLs. Media Foundation is
loaded on demand, so the program also starts on a Windows N edition without it
(MP3 recording then says so; WAV works).

## Build and run

```bat
build.bat              :: x64 + x86: the app and the unit tests, then runs the tests
build.bat x64          :: one architecture
build.bat x64 tests    :: just the unit tests (the quick edit-test loop)
build.bat hardware     :: also builds bin\<arch>\hardware_tests.exe (not run: it records)

bin\x64\audior_gui.exe
```

`build.bat` finds Visual Studio itself (via `vswhere`), so any command prompt
works. The binaries use audior's "hybrid CRT" settings (static VC runtime,
in-box Universal CRT).

No Visual Studio? Every push is built by CI on a Windows machine; download
`audior-gui-windows` from the run's *Artifacts* and unzip it.

**Setting up the compiler** (once, from any prompt; it asks for administrator
approval and downloads a few GB). This installs the Build Tools with the *Desktop
development with C++* workload: MSVC for x86 and x64 and the Windows SDK, which is
everything the project needs. Built and tested this way with Build Tools 17.14,
MSVC 14.44 and Windows SDK 10.0.26100.

```bat
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

## Every audior option, in the window and on the command line

| audior option | In the window | Command line |
|---|---|---|
| `-o, --output <file>` | **Save to** folder + **File name** box. Empty: `audior-<date>-<time>`. The extension follows the format (`meeting` becomes `meeting.mp3`, `clip.wav` with MP3 becomes `clip.mp3`). A file is never overwritten: a taken name becomes `meeting (2).mp3`. | `-o, --output <file>`, also starts recording |
| `-f, --format` | **Format** MP3 / WAV | `-f wav\|mp3` |
| `-b, --bitrate` | **Format** bitrate list (96 to 320 kbps) | `-b <kbps>` |
| `-m, --mic` | Microphone pane drop-down: every detected input device | `-m <id\|index\|name>` |
| `-a, --amplify` | Microphone pane **Gain** 0 to 300% | `-a <0-300>` |
| `-i, --include-output` | **System audio** checkbox | `-i` |
| `-d, --output-device` | System audio pane drop-down: every playback device | `-d <id\|index\|name>` |
| `--merge [percent]` | **One merged file** + **Share of the mix** slider (the microphone's share, default 65%) | `--merge [percent]` |
| `--separate` | **Two separate tracks**: `<name>_mic` and `<name>_system` | `--separate` |
| `-l, --list-devices` | The device drop-downs list them all | `-l` |
| `-s, --stop` | **Stop** | `-s` stops the recording in progress (audior's own `--stop` works too: same signal) |
| `-h, --help` | | `-h` |

Beyond audior: a **gain for system audio** (`--system-amplify <0-300>`), a
checkbox to leave the **microphone** out (system audio alone), `--demo`, and live
waveforms with a peak level and a clipping flag for every stream.

The command-line options only preset the window, so a shortcut such as

```bat
audior_gui.exe -o "%USERPROFILE%\Music\standup" -f wav -i --separate -a 50
```

opens the window already recording, with everything still adjustable.
`audior_gui --help` lists the options; `audior_gui --list-devices` prints the
devices as `audior --list-devices` does. Output goes to the console that started
the program, or to the file or pipe it was redirected to
(`audior_gui --list-devices | more`).

## How a recording behaves

- **Format.** Everything is converted to 48 kHz, 16-bit, stereo, whatever the
  device delivers (8 to 32-bit integer or float, mono to 7.1, any sample rate).
  WAV is uncompressed PCM; MP3 is constant bit rate from the in-box Media
  Foundation encoder.
- **Merge.** Both streams are mixed with weights that add up to 1, so a merged
  file cannot clip at any split. **Separate** writes two files that span the
  whole recording and stay aligned: while nothing is playing, the system track is
  filled with silence instead of ending short.
- **Streams start together.** Recording begins once every device is running. A
  Bluetooth headset's microphone needs half a second or more to switch profile; the
  audio the other device captured meanwhile is dropped, so the tracks are the same
  length and line up in a merged file.
- **Gain** is applied before the 16-bit clamp, and the waveform shows the stream
  after its gain, so clipping is visible. Merging weights come afterwards.
- **Memory stays bounded.** Audio is written to disk as it is recorded; each
  stream may buffer at most 8 seconds, and a disk that cannot keep up ends the
  recording instead of growing memory.
- **Failures keep your audio.** If a device vanishes, a disk fills up, or a file
  cannot be finalised, the recording stops at once, what was captured is
  finalised into a valid file, and the window says what happened (for example
  `Audio capture failed: the device was unplugged or switched off (hr=0x88890004)`).
  WAV is limited to 4 GiB, about 6 h 12 min.
- **Other programs keep their volume.** The recorder opts out of Windows'
  "communications activity" ducking.
- **Closing the window, `--stop`, or Stop** all finalise the files and release
  the devices (a Bluetooth headset returns to normal playback).
- **Stopping from outside.** While recording, the program holds the named event
  `Local\AudiorStopEvent`, the same one audior uses, so `audior_gui --stop` and
  `audior --stop` each end a recording started by either.

## Design

- **One pane per stream**, each with its own **checkbox** (is this stream
  recorded?), a **drop-down of every detected device** (refreshed whenever it
  opens, so a newly plugged-in microphone appears), a **gain** slider, a peak
  level in dB, a **CLIP** flag and its waveform. At least one stream must stay
  ticked.
- **Scrolling bars.** Bar heights follow a dB scale (about 54 dB of range),
  newest on the right. The playhead glides at the real-time rate even though audio
  arrives in bursts.
- **Settings lock while recording**, as audior's options do. Everything works from
  the keyboard: Tab moves between controls, Space / Enter activate, arrow keys
  adjust sliders and segmented controls, the file name box edits like any text
  box (Ctrl+A / C / X / V, Home / End, Ctrl+arrows).
- **Sharp everywhere.** Everything is laid out in DIPs; the window is
  per-monitor DPI aware.
- **Idle means idle.** The frame timer runs only while a recording or a waveform
  is moving.

Colours, sizes and the type scale live in `src/ui/theme.h` and `src/ui/gfx.cpp`.
To see the UI without opening it, render a scenario to a PNG:

```bat
bin\x64\audior_gui.exe --screenshot ui.png --scenario recording --size 1120x720
```

Scenarios: `idle`, `recording`, `mic-only`, `system-only`, `clipping`, `saved`,
`error`, `named`, `typing`.

## Architecture

```
  Win32 window -> MainView (layout, paint, input) -> RecorderController -> IRecordingEngine
  src/ui          src/ui                             src/core             src/engine
```

| Path | |
|------|--|
| `src/core` | No Windows or UI headers. Settings and their rules (mirroring audior's options and limits), the command line, the text-box editing model, formatting, `PeakHistory`, `PeakMeter`, `WaveformLane` (smooth scrolling model), `RecorderController` (the state machine) and the `IRecordingEngine` interface. |
| `src/engine` | `WasapiEngine` and `SimulatedEngine` behind that interface. See below. |
| `src/ui` | `AppWindow` (Win32, DPI, frame timer, dialogs, clipboard), `MainView`, `LaneView`, widgets, `Painter` (Direct2D), `Gfx` (factories and fonts), `screenshot`. |
| `src/main.cpp`, `src/console.*` | Entry point; console output for `--help`, `--list-devices`, `--stop`. |
| `tests` | Unit tests for everything outside `src/ui`, a tiny dependency-free harness, the import check, and `ui_smoke.ps1`. `tests/hardware`: the native hardware tests (`hardware_tests.exe`), which record on real devices. |

### The recording engine

```
 WasapiCapture (one thread per stream)           writer thread
   WASAPI shared mode, mic or loopback             every 100 ms
   -> PcmConverter (48 kHz / 16-bit / stereo,      rings -> MixWeighted (merge)
      gain, downmix, resample)                        -> WavSink / Mp3Sink
   -> peak bins to the waveform (100 per second)
   -> 8 s RingBuffer
```

`RecordingSession` owns the threads and takes its capture sources and file sinks
from factories, so every path after the capture (conversion, ring buffers, the
merge, failure handling, the start handshake) is tested with generated audio and
in-memory files. The GUI sees audio only as peak bins through
`IRecordingEngine::TakePeaks()`.

## Testing

- **Unit tests** (`run_tests.exe`, run by `build.bat`): settings and file-name
  rules, the command line, the text-box model, formatting, peak history and
  meter, waveform alignment, the controller state machine, the simulated engine,
  sample-format conversion and resampling, ring buffers, the mixer, the WAV and
  MP3 sinks, the recording session (merge, separate, device and disk failures,
  overflow, outside stop) and the engine's no-hardware paths.
- **Import check** (`tests\check_imports.ps1`): only in-box Windows DLLs.
- **UI smoke test** (`tests\ui_smoke.ps1`): launches the real executable, drives
  the command line, clicks Start and Stop in the simulated and the real engine,
  checks the status light and saves screenshots.
- **Hardware tests** (`hardware_tests.exe`, built by `build.bat hardware`, run by hand on a
  PC with a microphone and a playback device): about 100 checks over 14 scenarios, with
  recordings of at least 30 s each, on the real devices. Native C++ like the rest of the
  project; see [docs/HARDWARE-TESTS.md](docs/HARDWARE-TESTS.md).
- **CI** (`.github/workflows/ci.yml`): all of the above, warnings as errors, on a
  Windows runner for every push; screenshots and the executables are published
  as artifacts.

## Known limitations

- A recording begins when every device is running, so with a slow device (a Bluetooth
  headset's microphone takes 0.6 to 1.4 s) the first fraction of a second of the system
  audio is not recorded. The tracks are then the same length and line up.
- The hardware tests have run on one PC (a Bluetooth headset and two digital outputs);
  other devices, sample rates and wired microphones are untested.
- Drop-down lists use the system pop-up menu, which stays light on the dark UI.
- The UI is custom-drawn, so it does not yet expose UI Automation (screen readers).
- Settings are not remembered between runs.
- Shared mode only: a device another program holds exclusively cannot be recorded.
