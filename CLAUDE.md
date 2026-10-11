# audior-gui - working notes

A native Win32 GUI for [audior](https://github.com/thewongdirection/audior), the
WASAPI audio recorder. Windows only: C++17, Win32, Direct2D and DirectWrite.

## Hard constraints

- **Windows 10 and 11, and nothing else.** No Linux, Wine or cross-platform
  support or tooling. Use only APIs present on both; give Windows 11-only
  extras (fonts, DWM attributes) a graceful fallback.
- **Only WASAPI and libraries that ship with Windows.** Audio is WASAPI (and
  Media Foundation for MP3, as in audior); the UI is Win32, Direct2D,
  DirectWrite and WIC. No .NET, no WebView2, no third-party library for any
  functionality. `tests/check_imports.ps1` fails the build if the exe imports a
  DLL that is not on its allowlist of in-box Windows DLLs.
  - *One exception, narrowly:* the optional live-transcription engine. The
    recorder (`audior_gui.exe`) stays in-box only. Transcription runs in a
    separate helper process (`audior_asr.exe`, built by `build.bat asr` from
    `src/asr`) that uses a third-party inference library (whisper.cpp, vendored
    in `third_party`, CPU only so far); later it may use GPU backends, extra
    DLLs. It does no networking, and neither does the recorder (cloud engines
    are only a placeholder for now). The helper and its DLLs sit next to the exe, are started by
    absolute path and checked against a hash list first; models are files the
    user downloads (never in the repo); a notices file lists every third-party
    library and model licence. A failing helper never ends a recording.
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
| `src/core` | Logic with no Windows or UI headers: settings and their rules, the command line (`cli`), the text-box editing model, formatting, waveform and transcript models, controller, the `IRecordingEngine` seam (audio as peak bins, live transcripts as `TranscriptEvent`s), and `asr_protocol` (the byte stream between the recorder and its transcription helper, with a bounds-checked parser). |
| `src/engine` | Engines behind that seam: `WasapiEngine` (the real one: `RecordingSession` with capture and writer threads, `WasapiCapture`, WAV and Media Foundation MP3 sinks; it transcribes when the helper and a model are in place) and `SimulatedEngine` (`--demo`, tests; it plays a scripted transcript). The session takes sources and sinks from factories, so it is tested with generated audio and in-memory files. Transcription: `SpeechDecimator` (48 kHz stereo to 16 kHz mono) feeds `TranscriptionHost`, which talks to the helper through an `IAsrTransport`, restarts it when it dies and never blocks or fails a capture thread; tested against a fake helper on in-memory pipes. `ProcessTransport` is the real transport (the helper in a Job object, below-normal priority, three inherited handles), `asr_helper` finds the helper and a model and checks them against the manifest and the models table (`core/asr_models`, `core/sha256`); the process tests run `run_tests.exe` itself as a fake helper (`tests/fake_asr_helper`). |
| `src/ui` | Win32 + Direct2D window and widgets. |
| `src/asr`, `third_party` | The transcription helper `audior_asr.exe`: protocol loop, phrase cutting (`core/utterance_tracker`), decode scheduling (`core/decode_queue`) and whisper.cpp behind `WhisperDecoder`; whisper.cpp is a trimmed, unedited copy (see `third_party/README.md`). Every library and model licence is in `THIRD-PARTY-NOTICES.md`. Checked by hand with `audior_asr.exe --file x.wav --model m --realtime`, and by `tests/test_real_helper.cpp` (skipped without the helper and a model). |
| `src/console.*`, `src/main.cpp` | Entry point and console output for `--help`, `--list-devices`, `--stop`. |
| `tests` | Unit tests for everything outside `src/ui` (`run_tests.exe`), `check_imports.ps1`, and `ui_smoke.ps1`, which launches the real exe on a Windows machine. `tests/hardware` holds `hardware_tests.exe` (native C++, built by `build.bat hardware`): it records with the real exe on real devices and checks the files; see `docs/HARDWARE-TESTS.md`. |

`build.bat` builds x86 + x64 and runs the tests. Use `build.bat x64 tests` for
the quick edit-test loop.

**Parity with audior.** Every audior option has a control in the window and the
same flag on the command line (see the table in `README.md`); the GUI adds a
system-audio gain, per-stream on/off and per-stream live transcripts. A new
audior option needs all three: a setting + validation (`src/core/settings`), a
flag (`src/core/cli`) and a control (`src/ui/main_view`), with tests.

**What CI cannot do.** The runners have no audio devices, so the WASAPI capture
code itself (`wasapi_capture.cpp`, `devices.cpp`) is only exercised for its
failure paths. Everything after the capture (conversion, mixing, threads, sinks,
failure handling) is tested with fakes. Real-hardware checks are listed in
`docs/FIRST-TEST.md`; keep it current. The same goes for the transcription
helper: CI does not build it (`build.bat asr`) or have a model, so its speech
tests are skipped there and run by hand.

## Before any commit, push or merge

1. Thorough code review of the change; fix what it finds.
2. Add regression tests where they belong, then run the **whole** suite
   (`build.bat`) and fix every failure first.
3. Scan the tree for secrets (keys, tokens, credentials, personal paths) and
   for security problems before anything goes to GitHub.
4. Open pull requests as drafts.
