# audior-gui

A native Windows 10 / 11 recorder with a clean, modern UI: **one live waveform
for every audio stream being recorded**, big **Start** and **Stop** buttons, and
the recording options of the `audior` command-line recorder in a side panel.

> **Status: skeleton.** The window, layout, widgets, waveforms and the whole
> record/stop flow are done and tested, but the audio is *simulated* (the window
> says **DEMO**). Recording real audio is the next step; see
> [Recording engine](#recording-engine).

```
+----------------------------------------------------------------+
|  audior  [DEMO]                                      * Recording|
+------------------------------------------+---------------------+
|  * Microphone                    -12 dB  |  SAVE TO            |
|  Realtek Microphone Array                |  [ ...\audior      v]|
|  |||  ||||||  ||| ||||||||| |||||| |||   |  FORMAT             |
+------------------------------------------+  [MP3|WAV] [128 kbps]|
|  * System audio                  -20 dB  |  MICROPHONE         |
|  Speakers (Realtek)                      |  [Default         v] |
|  ||| |||||  |||||| ||||  |||||| ||| ||   |  Gain  ----o--- +0% |
|                                          |  SYSTEM AUDIO   (o) |
+------------------------------------------+  ...                |
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
Windows**. The UI is plain Win32 with Direct2D, DirectWrite and WIC.
`tests/check_imports.ps1` fails the build if the executable ever imports a DLL
outside an allowlist of in-box Windows DLLs.

## Build and run

```bat
build.bat              :: x64 + x86: the app and the unit tests, then runs the tests
build.bat x64          :: one architecture
build.bat x64 tests    :: just the unit tests (the quick edit-test loop)

bin\x64\audior_gui.exe
```

`build.bat` finds Visual Studio itself (via `vswhere`), so any command prompt
works. The binaries use the same "hybrid CRT" settings as audior (static VC
runtime, in-box Universal CRT).

To see the UI without opening it, render a scenario to a PNG:

```bat
bin\x64\audior_gui.exe --screenshot ui.png --scenario recording --size 1120x720
```

Scenarios: `idle`, `recording`, `mic-only`, `clipping`, `saved`, `error`.

## Design

- **One lane per stream.** The microphone lane is always there; the system-audio
  lane appears when *System audio* is switched on (otherwise a quiet placeholder
  keeps the layout stable). Each lane has its own accent colour, device name,
  peak level in dB and a **CLIP** flag, so `Gain` problems are visible.
- **Scrolling bars.** Bar heights follow a dB scale (about 54 dB of range), newest
  on the right. The playhead glides at the real-time rate even though an engine
  delivers audio in bursts, so the motion is smooth.
- **Settings lock while recording**, as audior's options do. Keyboard: Tab moves
  between controls, Space / Enter activate, arrow keys adjust sliders and
  segmented controls.
- **Sharp everywhere.** Everything is laid out in DIPs; the window is
  per-monitor DPI aware.
- **Idle means idle.** The frame timer runs only while a recording or a
  waveform is moving.

Colours, sizes and the type scale live in `src/ui/theme.h` and `src/ui/gfx.cpp`.

## Architecture

```
  Win32 window  ->  MainView (layout, paint, input)  ->  RecorderController  ->  IRecordingEngine
  src/ui            src/ui                               src/core                src/engine
```

| Path | |
|------|--|
| `src/core` | No Windows or UI headers. Settings and validation (mirroring audior's options and limits), formatting, `PeakHistory`, `PeakMeter`, `WaveformLane` (smooth scrolling model), `RecorderController` (the state machine), and the `IRecordingEngine` interface. |
| `src/engine` | Engines behind that interface. Today: `SimulatedEngine`. |
| `src/ui` | `AppWindow` (Win32, DPI, frame timer, dialogs), `MainView`, `LaneView`, widgets, `Painter` (Direct2D), `Gfx` (factories and fonts), `screenshot`. |
| `tests` | Unit tests for everything outside `src/ui`, a tiny dependency-free harness, and the import check. |

## Recording engine

The GUI never touches audio itself; it drives an `IRecordingEngine` and receives
audio only as **peak bins** (min/max per 10 ms) through `TakePeaks()`. The
simulation implements it today. The real engine will be a native **WASAPI**
implementation (microphone capture, loopback capture of the playback device,
resampling to 48 kHz / 16-bit / stereo, WAV and Media Foundation MP3 writers,
merge or separate files), reporting its peaks *after* the microphone gain so that
clipping shows in the waveform. Nothing else in the app changes when it lands.

## Testing

- **Unit tests** (`tests\run_tests.exe`, run by `build.bat`): settings rules,
  formatting, peak history and meter, waveform playhead and bar alignment, the
  simulated engine, and the controller state machine.
- **Import check** (`tests\check_imports.ps1`): only in-box Windows DLLs.
- **CI** (`.github/workflows/ci.yml`): builds with warnings as errors and runs the
  above on a Windows runner for every push.

## Known limitations

- Audio is simulated until the WASAPI engine is added.
- Drop-down lists use the system pop-up menu, which stays light on the dark UI.
- The UI is custom-drawn, so it does not yet expose UI Automation (screen readers).
- Settings are not remembered between runs.
- The device list is read once at start-up.
