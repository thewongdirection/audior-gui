# To come back to

What live transcription still lacks after its first working version. Nothing here
blocks using it; each item is worth doing when it starts to matter.

## Not yet checked on real hardware

- The microphone's own transcript, and the rest of `docs/FIRST-TEST.md` section 4 (killing
  the helper, a changed file, closing mid-sentence): a person has to speak into the
  microphone and watch the window. The system audio's transcript is checked by
  `hardware_tests.exe --only T15`, which makes the PC speak a sentence (CI has no audio
  devices, and the speech tests are skipped there for want of a model).
- Silero's thresholds on real conversations (`src/asr/silero_classifier.cpp`): on
  whisper.cpp's JFK sample it splits at every pause, which is right, but a very
  short pause may be too eager.

## Graphics card

- An NVIDIA card (RTX 5080) and, forced with `GGML_VK_VISIBLE_DEVICES=1`, an AMD Radeon
  iGPU have been tried: both transcribe, with the same text as the processor. The small
  model keeps up on the iGPU, the large turbo one does not (slower than the processor).
  Intel cards are untested; the first GPU in ggml's list is used, with no way to choose
  another.
- On NVIDIA about 80 % of the helper's processor time is one thread of the card's driver
  (`nvoglv64.dll`); it is zero while nobody speaks, about 0.15 core in a normal recording and
  up to about one core during minutes of unbroken speech, so "on the card" is not "free".
  Each decode makes a fresh whisper state (`WhisperDecoder::Decode`), allocating and freeing
  several hundred MB each time; reusing a state, or decoding the drafts of a long phrase less
  often, is the obvious thing to try and measure.
- The first use of the card takes about eight seconds (the driver builds its shader
  code); a pipeline cache kept between runs, or starting the helper earlier, would hide it.
- The large (turbo) model on the RTX 5080 keeps up (53 s of speech in 2.2 s when given at
  once, against 42.6 s on the processor).
- Nothing yet stops the card being shared badly with a game or a video encode: no check
  of free video memory, no lower GPU priority. The planned *Auto* choice would pick
  processor or card from what is free (video memory, load, battery).
- No CUDA backend (larger and faster on NVIDIA, but needs the CUDA toolkit).
- The first GPU in the list may be an integrated one on some PCs.

## Start-up set-up and downloads

- Not yet checked on real hardware: the logic is unit-tested and the download program has
  fetched a real model, but the DXGI scan on real cards, the notification in the real
  window, the tray balloons and a download on a PC with the real helper are hand checks;
  see `docs/FIRST-TEST.md`, section 5.
- The graphics card is judged from DXGI (a card that is not the software renderer) and the
  presence of `vulkan-1.dll`; whether the card really works with the helper's Vulkan backend
  is only known when the helper runs, and it then falls back to the processor by itself. A
  hybrid laptop's integrated card may be the one ggml picks (see above).
- A model is offered only when none is installed. A PC with the small model and a strong
  card is never offered the multilingual one; and the large model on a card has not been
  timed, which is also why it is only offered, never made the default.
- A download that is interrupted starts again from the beginning (no resume), and a proxy
  that needs a sign-in is not supported (WinHTTP uses the system's proxy settings, but the
  download program has no way to ask for credentials).
- Only the models of the table can be downloaded; adding a mirror or another host means
  changing `audior_fetch.exe`, which accepts `huggingface.co` only.

## Elsewhere

- Cloud transcription: a placeholder only (see the README); to be designed later.
- The helper is built for x64 only, and CI does not build it.

## Found in the review of the saved settings, the saved transcripts and the window fit

None of these loses a recording; each is worth doing when it starts to matter.

- **The window fit and the minimum size.** `FitToWorkArea` (`core/window_fit`) cuts the
  window down to the work area, but the window refuses to be smaller than
  `kMinHeight + ExtraMinHeight` (`WM_GETMINMAXINFO`): on a 1366x768 screen at 100 % the
  work area is shorter than that, so the bottom edge (the Start button) can end up under
  the taskbar. Give the fit the minimum size and keep the title bar inside the work area.
- **File writes on the wrong threads.** A transcript line is written to disk on the host's
  reader thread (`TranscriptFile::Add`): a stalled target (an offline share, a sleeping USB
  disk) holds up the helper's output and, on Stop, the UI thread. The settings file is
  read and written on the UI thread too, and its folder is looked at before even
  `--stop` or `--help` run (a saved folder on a dead share stalls every launch). A small
  writer thread, or a bounded queue, would take both off the UI.
- **A fallback is saved as if it were a choice.** A device that is not listed (the audio
  service not up yet, a USB microphone unplugged) or a folder that cannot be reached is
  replaced by the default and saved that way at exit, so the next run has forgotten it.
  Keep the loaded value in the file until the person changes it.
- **Saved settings and command-line runs.** The command line is applied over the saved
  settings, and `-i` or `--transcribe-*` can only switch things on: after a run with the
  system audio only, `-o call.mp3 -m 2` records no microphone, and a saved `wav` format
  turns `call.mp3` into `call.wav`. `--defaults` is the way out and is documented; a
  warning when `-o` starts a recording from saved settings that disagree would be kinder.
- **Smaller things.** A path such as `\Rec` or `C:Rec` counts as absolute
  (`IsAbsolutePath`) though it depends on where the program starts, and
  `saved_settings.cpp` says it is refused. The hotkeys kept in the registry by earlier
  versions are not carried over to the settings file. A failed save (a sync client holding
  the file, Controlled Folder Access) is silent and not retried, and a file the program
  does not accept (over 16 KB, UTF-16 without a mark) is replaced at exit; comments in it
  are not kept. The helper's lag report can reach the recorder out of order (a stale
  "catching up" can follow a later "live"). If the decode queue overflows, the transcript
  file can show a phrase twice or out of order. The " (2)" fallback of a transcript name
  is not checked against the path limit, and only "file exists" tries the next number.
  Nothing makes a new `RecordingSettings` field a field of the settings file: add a test
  that fails on a field it does not know. `hardware_tests.exe --only T15` records with
  the first installed model, so a slow PC can fail it for speed, not for a bug.
