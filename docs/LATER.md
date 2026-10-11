# To come back to

What live transcription still lacks after its first working version. Nothing here
blocks using it; each item is worth doing when it starts to matter.

## Not yet checked on real hardware

- A real recording with the helper (`docs/FIRST-TEST.md`, section 4): CI has no audio
  devices, and the speech tests are skipped there for want of a model.
- The step to a smaller model on a PC that is really too slow (the logic is tested
  with fake helpers; `core/lag_governor.h` has the thresholds: 3 s / 1.5 s behind to
  say *Catching up*, 6 s for 10 s to change model).
- Silero's thresholds on real conversations (`src/asr/silero_classifier.cpp`): on
  whisper.cpp's JFK sample it splits at every pause, which is right, but a very
  short pause may be too eager.

## Graphics card

- Only an NVIDIA card (RTX 5080) has been tried. AMD and Intel cards, and a PC with
  only an integrated GPU, are untested; the first GPU in ggml's list is used, with no
  way to choose another.
- The first use of the card takes about eight seconds (the driver builds its shader
  code); a pipeline cache kept between runs, or starting the helper earlier, would hide it.
- The large (turbo) model on the card has not been timed.
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
