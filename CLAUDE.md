# audior-gui - working notes

A native Win32 GUI for [audior](https://github.com/thewongdirection/audior), the
WASAPI audio recorder. Windows only: C++17, Win32, Direct2D and DirectWrite.

## Hard constraints

- **Windows 10 and 11, and nothing else.** No Linux, Wine or cross-platform
  support or tooling. Use only APIs present on both; give Windows 11-only
  extras (fonts, DWM attributes) a graceful fallback.
- **Only WASAPI and libraries that ship with Windows.** Audio is WASAPI (and
  Media Foundation for MP3, as in audior); the UI is Win32, Direct2D,
  DirectWrite and WIC; DXGI is asked which graphics cards there are. No .NET, no WebView2, no third-party library for any
  functionality. `tests/check_imports.ps1` fails the build if the exe imports a
  DLL that is not on its allowlist of in-box Windows DLLs.
  - *One exception, narrowly:* the optional live-transcription engine. The
    recorder (`audior_gui.exe`) stays in-box only. Transcription runs in a
    separate helper process (`audior_asr.exe`, built by `build.bat asr` from
    `src/asr`) that uses a third-party inference library (whisper.cpp, vendored
    in `third_party`) built as DLLs: ggml and whisper, a CPU backend and, if
    built with `build.bat vulkan`, a Vulkan backend for the graphics card. Those
    DLLs and the driver's `vulkan-1.dll` are the only non-Windows DLLs allowed,
    and only for the helper's own files (`tests/check_imports.ps1 -Allow`; the
    recorder is never checked with it). It does no networking, and neither does the recorder (cloud engines
    are only a placeholder for now). The helper and its DLLs sit next to the exe, are started by
    absolute path and checked against a hash list first; models are files that are
    never in the repo (the user downloads them, or the program does after the user says yes, see below); a notices file lists every third-party
    library and model licence. A failing helper never ends a recording.
  - *The one place that touches the network:* the download program `audior_fetch.exe`
    (`src/fetch`, built by every `build.bat`, with its own hash list `audior_fetch.sha256`).
    It uses only Windows' own WinHTTP (the one DLL `check_imports.ps1 -Allow winhttp`
    lets it import; the recorder and the helper must never import it), takes only an
    https address on `huggingface.co` (its redirects, to the CDN that really serves a file,
    may go to any https host: the SHA-256 checked by the recorder, not the host, vouches for
    the file), sends no cookies, writes only a new `*.part` file of at most the size
    it was told, and is started by the recorder (in a Job object, by absolute path,
    after its hash is checked) only after the user has answered *Download*. The recorder,
    not the program, decides what counts as a model: size and SHA-256 from
    `src/core/asr_models.h`, checked before the file gets its name. Deleting the program
    removes the ability to download. Any new network use belongs in that program,
    never in the recorder or the helper.
- **audior is private; this repository is public.** Never copy audior's source
  (or its docs) here. The recording engine in this repo is its own WASAPI
  implementation behind `IRecordingEngine`. audior's *behaviour* (options,
  limits, messages, `Local\AudiorStopEvent`) is matched on purpose.

## Engineering principles

- **Compact and efficient over lazy.** Prefer the small, direct, well-factored
  solution to the quick one that wastes work. No copy-paste, no dead code, no
  speculative abstraction.
- **Keep resource use in control.**
  - *Memory:* fixed-size buffers sized to what is needed; nothing that grows
    with recording length; no heap allocation per frame or per tick (reuse
    buffers); no oversized scratch buffers, in tests as well.
  - *CPU:* animate and poll only while something is moving; draw only what is
    visible.
  - *Disk:* build intermediates go in `build\` (git-ignored), nothing is left
    in the repo, and temporary files, downloads, caches and toolchain
    installers are removed as soon as they are no longer needed.
- Code that reads like its surroundings: match the existing naming, comment
  density and idiom. Sources are UTF-8 (`build.bat` compiles with `/utf-8`).

## Layout

| Path | What |
|------|------|
| `src/core` | Logic with no Windows or UI headers: settings and their rules, the text of the settings file (`saved_settings`), the command line (`cli`), the hotkey rules (`hotkey`), the text-box editing model, formatting, waveform and transcript models, controller, the `IRecordingEngine` seam (audio as peak bins, live transcripts as `TranscriptEvent`s), `asr_protocol` (the byte stream between the recorder and its transcription helper, with a bounds-checked parser), and the start-up set-up of transcription: `setup_plan` (what a hardware scan decides: use the graphics card or not, which models to offer), `setup_status` (the notification's phases and words, and the `ISetupSource` the controller polls) and `fetch_protocol` (what `audior_fetch.exe` says, with a bounded line reader). |
| `src/engine` | Engines behind that seam: `WasapiEngine` (the real one: `RecordingSession` with capture and writer threads, `WasapiCapture`, WAV and Media Foundation MP3 sinks; it transcribes when the helper and a model are in place) and `SimulatedEngine` (`--demo`, tests; it plays a scripted transcript). The session takes sources and sinks from factories, so it is tested with generated audio and in-memory files. Transcription: `SpeechDecimator` (48 kHz stereo to 16 kHz mono) feeds `TranscriptionHost`, which talks to the helper through an `IAsrTransport`, restarts it when it dies, changes to a smaller model when it cannot keep up (`core/lag_governor`) and never blocks or fails a capture thread; tested against a fake helper on in-memory pipes. `ProcessTransport` is the real transport (the helper in a Job object, below-normal priority, three inherited handles), `asr_helper` finds the helper and a model and checks them against the manifest and the models table (`core/asr_models`, `core/sha256`); the process tests run `run_tests.exe` itself as a fake helper (`tests/fake_asr_helper`). Start-up set-up: `hardware_scan` (DXGI adapters, Vulkan loader), `TranscriptionSetup` (a thread: scan, plan, ask, download, finish; all its dependencies are injected functions, so it is tested with made-up ones), `ModelInstaller` (runs the download program, checks size and SHA-256, moves the file into place, leaves no `.part`; tested against `run_tests.exe` as a fake download program, `tests/fake_fetch`) and `system_setup` (the real wiring, plus the remembered "not now" in `preferences`). |
| `src/fetch` | The download program `audior_fetch.exe` (WinHTTP; see "Hard constraints"). `tests/test_real_fetcher.cpp` runs the real one for what it refuses before it touches the network. |
| `src/ui` | Win32 + Direct2D window and widgets, the tray icon, the system-wide hotkeys. |
| `src/asr`, `third_party` | The transcription helper `audior_asr.exe`: protocol loop, phrase cutting (`core/utterance_tracker`), decode scheduling (`core/decode_queue`) and whisper.cpp behind `WhisperDecoder` or its parakeet library behind `ParakeetDecoder` (both an `IDecoder`; the recorder names the engine in `Hello`); whisper.cpp is a trimmed, unedited copy (see `third_party/README.md`). Every library and model licence is in `THIRD-PARTY-NOTICES.md`. Checked by hand with `audior_asr.exe --file x.wav --model m --realtime`, and by `tests/test_real_helper.cpp` (skipped without the helper and a model). |
| `src/console.*`, `src/main.cpp` | Entry point and console output for `--help`, `--list-devices`, `--stop`. |
| `src/preferences.*` | What is remembered between runs: the settings of the last run and the two hotkeys, in `Audior-GUI.ini` beside the exe (read in `main`, written by `AppWindow` when a recording starts and when the program ends; `--demo` and `--defaults` runs never write it). Built into the unit tests with `src/core` and `src/engine`. The offer to download a speech model remembers a "not now" as its `modelDownload` line (`SavedSettings::downloadDeclined`). |
| `docs/screenshots` | The pictures the README and the docs show (see "Documentation screenshots" below). |
| `tests` | Unit tests for everything outside `src/ui` (`run_tests.exe`), `check_imports.ps1`, and `ui_smoke.ps1`, which launches the real exe on a Windows machine. `tests/hardware` holds `hardware_tests.exe` (native C++, built by `build.bat hardware`): it records with the real exe on real devices and checks the files; see `docs/HARDWARE-TESTS.md`. |

`build.bat` builds x86 + x64 and runs the tests. Use `build.bat x64 tests` for
the quick edit-test loop.

**Documentation screenshots.** The README and `docs/` show the real window, in
`docs/screenshots/<scenario>.png`, drawn by the program itself (`--screenshot`,
simulated audio, fixed sample paths: nothing from a real PC). They are generated,
never edited by hand or captured from a desktop: `build.bat x64 screenshots`
redraws them all. Use a picture wherever it explains more than the text; a new
screen or feature gets a scenario (`src/ui/screenshot.cpp`), an entry in the
`:shots` list of `build.bat`, and a picture in the docs.

**Parity with audior.** Every audior option has a control in the window and the
same flag on the command line (see the table in `README.md`); the GUI adds a
system-audio gain, per-stream on/off and per-stream live transcripts. A new
audior option needs all three: a setting + validation (`src/core/settings`), a
flag (`src/core/cli`) and a control (`src/ui/main_view`), with tests. The one
deliberate difference in naming: what the user sees by default carries this
program's name, not audior's (the default folder `Documents\Audior-GUI` and the
default file name `Audior-GUI-<date>-<time>`).

**What CI cannot do.** The runners have no audio devices, so the WASAPI capture
code itself (`wasapi_capture.cpp`, `devices.cpp`) is only exercised for its
failure paths. Everything after the capture (conversion, mixing, threads, sinks,
failure handling) is tested with fakes. Real-hardware checks are listed in
`docs/FIRST-TEST.md`; keep it current. The same goes for the transcription
helper: CI does not build it (`build.bat asr`) or have a model, so its speech
tests are skipped there and run by hand. The DXGI scan is only smoke-tested (a
runner has no graphics card), and an actual download is not run in CI (no network
in the tests); both are in `docs/FIRST-TEST.md`.

## Before any commit, push or merge

1. Thorough code review of the change; fix what it finds.
2. Add regression tests where they belong, then run the **whole** suite
   (`build.bat`) and fix every failure first.
3. Scan the tree for secrets (keys, tokens, credentials, personal paths) and
   for security problems before anything goes to GitHub.
4. **Redraw the documentation screenshots when the UI changed.** If the change
   alters how the window looks or what a screen shows (anything in `src/ui`, a
   changed default text, a new or changed scenario), run `build.bat x64 screenshots`,
   look at the pictures, and commit them with the change; fix the docs that describe
   them. Old pictures must never outlive the UI they show. (CI warns when `src/ui`
   changes without `docs/screenshots`.)
5. Open pull requests as drafts.
