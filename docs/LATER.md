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

## Elsewhere

- Cloud transcription: a placeholder only (see the README); to be designed later.
- The helper is built for x64 only, and CI does not build it.
- Models are downloaded by hand; the recorder has no networking code, so an in-app
  download would have to be a separate small program.
