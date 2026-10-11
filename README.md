# Audior-GUI

A native Windows 10 / 11 recorder with a clean, modern UI: **one live waveform
for every audio stream being recorded**, big **Start** and **Stop** buttons, and
**every option of the `audior` command-line recorder** in a side panel. It lives
in the tray while minimized and records from **system-wide hotkeys** (Ctrl+Alt+R and
Ctrl+Alt+S, yours to change).

> **Status: verified on real hardware.** The WASAPI recording engine is covered by
> tests that run on generated audio, and `hardware_tests.exe` has recorded with the
> real application on a real PC (100 checks, all pass): microphone and system audio,
> MP3 and WAV, gains, device selection, long-recording stability. The first run found
> that a Bluetooth microphone, which takes most of a second to start, left the system
> track about 0.6 s ahead of the microphone track; that is fixed (see
> [docs/HARDWARE-TESTS.md](docs/HARDWARE-TESTS.md)). To try it yourself follow
> [docs/FIRST-TEST.md](docs/FIRST-TEST.md). `--demo` runs the window with simulated
> audio and needs no devices.

![Audior-GUI while recording: a live waveform for the microphone and for system audio on the left, the settings panel with the save folder, file name, format, output mode and hotkeys on the right, the Stop and Start buttons below](docs/screenshots/recording.png)

*The window while recording. Every picture in these docs is drawn by the program
itself from simulated audio (hence the DEMO badge, and the sample folder), so none
shows a real PC; `build.bat x64 screenshots` redraws them (see [Design](#design)).*

## Requirements

### To run

| | |
|---|---|
| Windows | Windows 10 or 11, x64 or x86, any edition. A Windows N edition lacks Media Foundation, so MP3 is unavailable there until the Media Feature Pack is installed; WAV works |
| Audio hardware | At least one device: a **microphone** (any capture device) and/or a **playback device**, whose output is recorded as *System audio* (WASAPI loopback). With neither, `--demo` runs the window on simulated audio |
| Windows privacy | Settings → Privacy & security → Microphone must let desktop apps use the microphone. If Windows blocks it, the recorder reports `access was denied (check Windows privacy settings)` or gets no microphone audio |
| Rights | An ordinary user account: no administrator rights, installer, driver or service |
| Display | Direct2D and DirectWrite, which are part of Windows. No GPU is required (Direct2D falls back to software). Segoe UI Variable on Windows 11, Segoe UI on Windows 10 |
| Disk | A writable folder (default `Documents\Audior-GUI`). At 48 kHz, 16-bit, stereo, WAV takes about 660 MiB per hour per file and MP3 about 55 MiB per hour at 128 kbps (137 MiB at 320 kbps); two separate tracks are two files. A full disk ends the recording and keeps what was captured |
| Memory | Fixed: each stream buffers at most 8 seconds, whatever the length of the recording |
| Network | **None.** No connection, account or API key. See [below](#what-works-offline-and-what-needs-the-internet) |
| Not needed | .NET, the Visual C++ redistributable, WebView2, any third-party library |

### To build

Visual Studio 2022 or its Build Tools with the *Desktop development with C++*
workload (MSVC for x86 and x64, and the Windows SDK), plus the in-box Windows
PowerShell, which runs the import check. `build.bat` finds Visual Studio itself and
downloads nothing: there is no package manager and no third-party code to fetch.

Like audior, the app uses **only WASAPI (for audio) and libraries that ship with
Windows**: Media Foundation for MP3, Win32 with Direct2D, DirectWrite and WIC for
the window. `tests/check_imports.ps1` fails the build if the executable ever
imports a DLL outside an allowlist of in-box Windows DLLs. Media Foundation is
loaded on demand, so the program also starts on a Windows N edition without it
(MP3 recording then says so; WAV works).

## What works offline, and what needs the internet

**Today everything is local.** Recording both streams, the gains, the merge, WAV and
MP3 encoding, the waveforms and every command-line option work with no connection,
no account and no key. The program sends nothing anywhere: it contains no networking
code, and `tests\check_imports.ps1` fails the build if the executable ever imports a
networking DLL. Unplugging the network changes nothing.

**Live transcription is planned, not built.** `main` has no transcription code yet.
The design is a live transcript scrolling in each stream's pane, local first; details
may change before it ships.

| | Offline: only what is installed on the PC | Online: an internet connection and an API key |
|---|---|---|
| Recording, mixing, MP3 and WAV, waveforms, command line | All of it (available now) | No difference: recording never uses the network |
| Live transcript (planned) | A speech-to-text model that runs on your own CPU, with GPU acceleration to follow. You download the model file once; it is never bundled with the program. After that no connection is needed and your audio never leaves the PC | Streaming transcription by a cloud service (Deepgram is the first planned). Needs a connection and your own API key, and your audio is sent to that service. Off unless you switch it on, and the pane shows when it is in use |
| Where the key lives (planned) | Not needed | Windows Credential Manager. Never in the settings, on the command line or in a log |

The internet would be used for exactly two things: the one-time download of a model,
and cloud transcription if you turn it on. An *Auto* choice would pick a local model
from what the PC has free (GPU memory, CPU, battery) and never use the cloud unless
you enabled it. Transcription would run in a separate helper program, so neither a
model nor a GPU driver problem can touch a recording, and the recorder itself stays
dependency-free. A transcription problem never stops or damages a recording.

## Build and run

```bat
build.bat              :: x64 + x86: the app and the unit tests, then runs the tests
build.bat x64          :: one architecture
build.bat x64 tests    :: just the unit tests (the quick edit-test loop)
build.bat hardware     :: also builds bin\<arch>\hardware_tests.exe (not run: it records)

bin\x64\Audior-GUI.exe
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
| `-o, --output <file>` | **Save to** folder + **File name** box. Empty: `Audior-GUI-<date>-<time>`. The extension follows the format (`meeting` becomes `meeting.mp3`, `clip.wav` with MP3 becomes `clip.mp3`). A file is never overwritten: a taken name becomes `meeting (2).mp3`. | `-o, --output <file>`, also starts recording |
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
Audior-GUI.exe -o "%USERPROFILE%\Music\standup" -f wav -i --separate -a 50
```

opens the window already recording, with everything still adjustable.
`Audior-GUI --help` lists the options; `Audior-GUI --list-devices` prints the
devices as `audior --list-devices` does. Output goes to the console that started
the program, or to the file or pipe it was redirected to
(`Audior-GUI --list-devices | more`).

## How a recording behaves

- **Format.** Everything is converted to 48 kHz, 16-bit, stereo, whatever the
  device delivers (8 to 32-bit integer or float, mono to 7.1, any sample rate).
  WAV is uncompressed PCM; MP3 is constant bit rate from the in-box Media
  Foundation encoder.
- **Merge.** Both streams are mixed with weights that add up to 1, so a merged
  file cannot clip at any split. **Separate** writes two files that span the
  whole recording and stay aligned. All streams share one clock, which starts when
  the first device is running; a stream that is not delivering is filled with
  silence up to it, both while nothing is playing (a playback device sends nothing
  when idle) and while a device is still starting (a Bluetooth microphone needs most
  of a second), so the tracks begin together and end together.
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

  ![A recording that ended with a problem: the status pill says Problem, the bottom bar says Audio capture failed and that the audio captured before the problem was kept, and a Show in folder button is offered](docs/screenshots/error.png)

- **Other programs keep their volume.** The recorder opts out of Windows'
  "communications activity" ducking.
- **Bluetooth headsets switch to call mode.** Opening a headset's microphone makes
  Windows switch it from stereo music mode to its hands-free (phone call) mode, and
  back when the recording ends: while recording, the headset's output drops out for
  about half a second, its volume steps (40 to 45 on the one tested) and the sound is
  thinner. That is the headset and Windows, not this program, and it cannot be avoided
  while the microphone is open, so the microphone pane says so when the chosen
  microphone is Bluetooth, with a **Record system audio only** link that unticks the
  microphone and ticks system audio in one click. System audio only does not trigger
  the switch, nor does a microphone that is not Bluetooth. Measurements in
  [docs/HARDWARE-TESTS.md](docs/HARDWARE-TESTS.md), finding 6.

  ![The microphone pane with a Bluetooth headset chosen: an amber note says the headset switches to call mode while recording, and a Record system audio only link sits on the right](docs/screenshots/bluetooth-mic.png)
- **Closing the window, `--stop`, or Stop** all finalise the files and release
  the devices (a Bluetooth headset returns to normal playback).
- **Stopping from outside.** While recording, the program holds the named event
  `Local\AudiorStopEvent`, the same one audior uses, so `Audior-GUI --stop` and
  `audior --stop` each end a recording started by either.

## Tray and hotkeys

- **Minimizing goes to the tray.** The window leaves the taskbar and the Audior-GUI
  icon appears in the notification area: a ring while idle, a red dot while
  recording, with the elapsed time in its tooltip. Click the icon to bring the
  window back; right-click it for *Show*, *Start recording*, *Stop* and *Exit*. A
  recording carries on while the window is in the tray, and a balloon says so if
  one ends with a problem there. A program started minimized (a shortcut set to
  *Run: Minimized*) goes straight to the tray. Closing the window with the X still
  stops the recording and exits.
- **System-wide hotkeys.** **Ctrl+Alt+R** starts a recording and **Ctrl+Alt+S** stops it,
  in whatever program has the focus, with the window open or in the tray. They do
  what the buttons do, so Start is ignored while recording and Stop while idle.
- **Yours to change.** In the *Hotkeys* section of the settings panel click a box
  and press the new shortcut (Esc cancels, and a shortcut that cannot be used says
  why and keeps listening). A shortcut is a letter, digit, function key (F1 to
  F24), Space or a navigation key, with Ctrl and/or Alt (and Shift if you like);
  a function key may stand alone. Start and Stop must differ, and a shortcut that
  another program already owns is refused. The pair is remembered for the next run
  (`HKEY_CURRENT_USER\Software\Audior-GUI`); if a remembered shortcut cannot be
  taken at start-up, for instance because a second copy of the program is running,
  its box is outlined in red and it stays off.
- **A global shortcut is taken from every other program** while Audior-GUI runs.
  The defaults avoid Ctrl+R and Ctrl+S, which other programs use for *reload* and
  *save*. On a keyboard layout where AltGr (which Windows treats as Ctrl+Alt) types
  letters, though, Ctrl+Alt+S or Ctrl+Alt+R may be a character you need (the Polish
  layout types ś with AltGr+S); choose another pair, such as two function keys.

![The Hotkeys section of the settings panel with the Start box listening: it shows Press keys, and the hint under the boxes says to press the new shortcut or Esc to cancel](docs/screenshots/hotkeys.png)

## Design

- **One pane per stream**, each with its own **checkbox** (is this stream
  recorded?), a **drop-down of every detected device** (refreshed whenever it
  opens, so a newly plugged-in microphone appears), a **gain** slider, a peak
  level in dB, a **CLIP** flag and its waveform. At least one stream must stay
  ticked.

  ![The microphone at 300% gain clipping: a red CLIP flag and 0 dB above its waveform, while the system audio stream stays below full scale](docs/screenshots/clipping.png)
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
  is moving, and a window hidden in the tray draws nothing and ticks only five
  times a second.

Colours, sizes and the type scale live in `src/ui/theme.h` and `src/ui/gfx.cpp`.
To see the UI without opening it, render a scenario to a PNG:

```bat
bin\x64\Audior-GUI.exe --screenshot ui.png --scenario recording --size 1120x720
```

Scenarios: `idle`, `recording`, `mic-only`, `system-only`, `bluetooth-mic` (a
headset microphone chosen), `clipping`, `saved`, `error`, `named`, `typing`,
`hotkeys` (a shortcut box waiting for keys), `hotkey-taken` (a shortcut another
program owns).

The pictures in this README and in `docs/` are such scenarios, kept in
`docs/screenshots/` (named after their scenario, drawn at 1120x720 from simulated
audio, so they hold nothing from a real PC). **When the window changes, redraw
them and commit the new pictures with the change:**

```bat
build.bat x64 screenshots      :: builds, then renders every picture the docs use
```

If a change adds a picture to the docs, add its scenario to the `:shots` list in
`build.bat` as well.

## Architecture

```
  Win32 window -> MainView (layout, paint, input) -> RecorderController -> IRecordingEngine
  src/ui          src/ui                             src/core             src/engine
```

| Path | |
|------|--|
| `src/core` | No Windows or UI headers. Settings and their rules (mirroring audior's options and limits), the command line, the hotkey rules and their text form, the text-box editing model, formatting, `PeakHistory`, `PeakMeter`, `WaveformLane` (smooth scrolling model), `RecorderController` (the state machine) and the `IRecordingEngine` interface. |
| `src/engine` | `WasapiEngine` and `SimulatedEngine` behind that interface. See below. |
| `src/ui` | `AppWindow` (Win32, DPI, frame timer, tray, hotkeys, dialogs, clipboard), `MainView`, `LaneView`, widgets, `TrayIcon` (and the icon, drawn in code), `Painter` (Direct2D), `Gfx` (factories and fonts), `screenshot`. |
| `src/main.cpp`, `src/console.*`, `src/preferences.*` | Entry point; console output for `--help`, `--list-devices`, `--stop`; the saved hotkeys (registry). |
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
  rules, the command line, the hotkey rules (which combinations are allowed, their
  text form) and their saving, the text-box model, formatting, peak history and
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

- Recording from a Bluetooth headset's microphone switches the headset to call mode for
  the duration (see *How a recording behaves*).
- A microphone whose clock runs faster than the PC's is not trimmed, so a very long
  recording could show a small difference between separate tracks.
- Every `Audior-GUI.exe` ends on one machine-wide stop event, so a second recording
  program instance, or `--stop` from another one, stops them all.
- The hardware tests have run on one PC (a Bluetooth headset and two digital outputs);
  other devices, sample rates and wired microphones are untested.
- Drop-down lists use the system pop-up menu, which stays light on the dark UI.
- The UI is custom-drawn, so it does not yet expose UI Automation (screen readers).
- The recording settings are not remembered between runs (only the two hotkeys are).
- The tray and the hotkeys are exercised by hand (see
  [docs/FIRST-TEST.md](docs/FIRST-TEST.md)); `ui_smoke.ps1` does not cover them.
- Shared mode only: a device another program holds exclusively cannot be recorded.
