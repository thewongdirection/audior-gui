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

## Elsewhere

- Cloud transcription: a placeholder only (see the README); to be designed later.
- The helper is built for x64 only, and CI does not build it.
- Models are downloaded by hand; the recorder has no networking code, so an in-app
  download would have to be a separate small program.
