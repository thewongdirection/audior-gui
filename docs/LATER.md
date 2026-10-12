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
- The helper's processor time on a card of its own is now small: 54 s of speech with the
  large model took 3.9 s of processor time (0.07 core) instead of 11 to 12 s (0.2 core),
  because the card's state is kept between decodes and the model gets its whole 30 s window
  (`WhisperDecoder::Decode`; the shortened window also made the large model repeat itself,
  62% extra words). An integrated GPU and the processor keep the old way, which is the
  faster one there (the full window made the small model, which has since been dropped, 2.5 s
  behind on the Radeon iGPU and the large one unusable). Whisper large-v3 at full precision (the top-end model, offered
  to a card with 8 GiB) used far more processor time on the card than the 5-bit turbo one
  (1.06 core against 0.07 in the same test; the card was shared with another program using
  most of its memory at the time), so memory on the card matters: nothing checks it yet.
- Parakeet has one size on offer (q8_0, 638 MiB); the smaller (q4_k) and bigger (f16) files
  of the same repository run in the same library but are not in the table, and a step down
  from Whisper large-v3 to Parakeet, which is lighter, is not made (a step down goes to the
  models before it in the table, of the same engine only).
- Parakeet understands 25 European languages. It was only tried with English speech (this PC
  has English voices only); a recording in another language is worth trying, and the Auto /
  English switch is greyed out for it by design.
- There is no way yet to download one more model from the window once a model is installed
  (the start-up offer only comes when none is); a "More models" choice would do it.
- The first use of the card takes about eight seconds (the driver builds its shader
  code); a pipeline cache kept between runs, or starting the helper earlier, would hide it.
- The large (turbo) model on the RTX 5080 keeps up (53 s of speech in 2.2 s when given at
  once, against 42.6 s on the processor), and also at the pace of speech through the whole
  path: 0.3 s behind on average, 0.5 s at most, against 1.7 s and 4.3 s on the processor
  (`hardware_tests.exe --only T17`).
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
- A model is offered only when none is installed. A PC with Parakeet and a strong card is never
  offered Whisper large-v3 afterwards (there is no "More models" choice yet); and the large model
  on a card has not been timed on every card, which is also why it is only offered, never made the
  default.
- **PCs without an 8 GiB card are offered Parakeet only** (since the small English model was dropped),
  which has no Chinese, Japanese, Korean, Arabic or Hindi. Such a PC can use Whisper turbo if the person
  puts the file in the models folder, but it is slow without a card (about seven cores for real-time
  speech), and nothing lighter that understands those languages is in the table. A middle Whisper
  model for them (a smaller multilingual one) is the open question.
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

## Found in the review of the start-up set-up and the download

No blocker; each is worth doing when it starts to matter.

- **A download during a recording.** The fetcher runs at below-normal CPU priority, but its disk
  priority is normal, and the SHA-256 pass and the move run at normal priority: on one slow disk
  they can stall the recording's writer (its ring is 8 s). The free-space check (64 MiB spare) looks
  at the models folder's drive, not at what the recording needs. Background I/O priority for the
  fetcher and the hash, or holding the install while a recording runs, would cover it.
- **A partial install.** The first model of the offer is fetched first: cancelling or failing during a later
  one leaves it installed, but the notification still says "Live transcripts stay off", "Try again"
  finds nothing missing and says "ready", and the README's "leaves nothing behind" is not true of
  the finished files. A crash or End Task during a download leaves its `.part` (up to about
  550 MiB) until the next install of that file; sweep `*.part` of the table at start.
- **The graphics card.** The 4 GiB rule is an exact `>=` (a "4 GB" card often reports a little
  less) and a 32-bit program is only told "4 GiB less a byte" at most; the Vulkan judgement is
  "the loader is in System32" (a loader next to the helper is not seen, one for an old card is),
  and virtual adapters other than Microsoft's (VMware, VirtualBox) count as cards. The "ready"
  text names the card even when `--transcribe-cpu` or the GPU switch says otherwise, and a PC
  without a usable card gets a disabled GPU switch without an explanation once the banner is gone.
  `ScanHardware` has no stop check, so closing the window waits for a slow driver.
- **Installed means the right size.** A model of the right size but wrong content (a power loss
  after the rename: the fetcher and the move do not flush) counts as installed, is never offered
  again, and fails its hash when a recording starts. Delete or re-offer a model that fails its hash.
- **The banner.** At the smallest width (940 DIP) long texts are cut without an ellipsis (the
  no-Vulkan sentence, "(3 of 3)" in the title); its height is not part of `ExtraMinHeight`, so a
  recording with the banner up has about two lines of transcript, and when it appears or goes
  after 10 s the cards move under the pointer. Every poll (4 a second while it moves) does a full
  `Refresh` and redraws the whole window. The tray icon is restored after an Explorer restart with
  the flags of the last call (`TrayIcon::Restore`), and the one balloon for a waiting question can
  be lost to Focus Assist: a tooltip suffix such as "needs your answer" would help.
- **The download program.** Redirects may go to any https host and port, and WinHTTP's automatic
  logon stays on (follow redirects by hand and check each host, or use the high logon level). It is
  started with the default DLL search and no process mitigations (`/DEPENDENTLOADFLAG`, block
  non-Microsoft binaries), runs with the recorder's full token, and its hash is checked without
  holding the file open until `CreateProcess` returns. Its path rule is "a full path ending in
  `.part`": `\\?\` paths, `..` and `:stream` names pass, and a reparse point at the `.part` is
  followed. A verified file is thrown away after one failed move (retry for a second or two), and
  the import allowlist of the one program with the network is the recorder's list plus `winhttp`.
- **"Not now"** is saved with the other settings (at a recording start, a hotkey change and exit):
  a crash loses it, and where the program folder cannot be written the offer returns at every start.
  `RememberSettings` copying the flag is not covered by a test; nor is the real DXGI call
  (`ProfileFromDesc` could be pulled out and tested).


## Chinese and the other languages (found when Chinese was tried)

- **What was wrong.** Parakeet knows 25 European languages and no Chinese: heard in Chinese it writes
  something Dutch or German looking (`We zijn maar...`). That is its limit, not a bug, and is now said in
  the window (the language field reads *European languages*) and the README. With *Auto*, Whisper large-v3
  turbo (5-bit) transcribed a Chinese talk in Simplified and Traditional characters by turns, with the
  commas written as in English; naming the language (*Chinese*) fixed the script (a Simplified sample
  sentence starts the text), and Chinese marks after a Chinese character are written full width.
  Checked by hand with one recording only (a talk in Mandarin, 30 s); no reference text, so no error rate.
- **Whisper's own Chinese could be better.** The published character error rates of large-v3 (from a web
  search for these notes, not measured here) are 5 % (AISHELL-2) to 10-19 % (WenetSpeech), and the turbo and
  quantised models are worse. Three ways to better, none done (the figures are the models' own cards and
  papers, not checked here):
  1. A Chinese fine-tune of Whisper, which runs in the helper unchanged (Belle-whisper-large-v3-zh, and its
     turbo version, Apache-2.0; 2 to 3 times lower error on AISHELL and WenetSpeech by their own tables).
     Their ggml files are third-party conversions on Hugging Face (about 0.6 GB for the turbo one at 5 bits,
     1.1 to 1.7 GB for large-v3), so they would need new table entries with hashes, and English quality after
     the fine-tune is unmeasured: probably one model per language, chosen in the window.
  2. Qwen3-ASR (Apache-2.0, January 2026; 0.6 and 1.7 billion parameters; Chinese error roughly half of
     Whisper large-v3's, English a little better, 30 languages and 22 Chinese dialects). whisper.cpp cannot
     run it: it needs llama.cpp (`mtmd`, build b9173 or later), `transcribe.cpp`, CrispASR or `qwenasr.cpp`,
     each with its own ggml: a second engine behind `IDecoder` in its own folder (the DLL names clash
     with whisper's), a bigger helper, and unknown speed on a card and on the processor. The one speed
     figure found is 2.6 s for 6.6 s of speech on an Apple M3.
  3. SenseVoice-Small (about 250 MB, zh/yue/en/ja/ko, very fast, under the FunASR licence, which needs a
     notice), through CrispASR or `transcribe.cpp`.
  No newer Whisper than large-v3 (and turbo) or Parakeet than v3 exists as of October 2026, and nothing
  of NVIDIA's that is open covers Chinese.
- **Auto still guesses each phrase** on its own: speech that mixes two languages can flip. A person who
  speaks English and Mandarin could be served by comparing only those two languages' probabilities and keeping
  the last choice when the margin is small (`whisper_lang_auto_detect`); not done.
- **The loop guard counts a Chinese character as half a word** (a heuristic: a one-character repeat needs
  twelve copies, a phrase of ten characters said twice counts as long). Thai, Lao, Burmese and Khmer are
  treated alike, Japanese kana too, none of them tried.
- **Languages of the drop-down:** the twenty of the table (`core/asr_languages.h`); the others Whisper knows
  (about eighty) can only be had through *Auto*. Cantonese (`yue`) has no entry because its prompt has not
  been tried.
