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
- **audior is private; this repository is public.** Never copy audior's source
  (or its docs) here. The recording engine in this repo is its own WASAPI
  implementation behind `IRecordingEngine`.

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
  density and idiom.

## Layout

| Path | What |
|------|------|
| `src/core` | Logic with no Windows or UI headers: settings, formatting, waveform model, controller, the `IRecordingEngine` seam. |
| `src/engine` | Engines behind that seam (the simulation; audior's `CRecorder` later). |
| `src/ui` | Win32 + Direct2D window and widgets. |
| `tests` | Unit tests for everything outside `src/ui` (`run_tests.exe`). |

`build.bat` builds x86 + x64 and runs the tests. Use `build.bat x64 tests` for
the quick edit-test loop.

## Before any commit, push or merge

1. Thorough code review of the change; fix what it finds.
2. Add regression tests where they belong, then run the **whole** suite
   (`build.bat`) and fix every failure first.
3. Scan the tree for secrets (keys, tokens, credentials, personal paths) and
   for security problems before anything goes to GitHub.
4. Open pull requests as drafts.
