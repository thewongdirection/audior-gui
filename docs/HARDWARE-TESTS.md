# Hardware tests

CI runners have no audio devices, so the capture path (`wasapi_capture.cpp`,
`devices.cpp`) is only unit-tested for its failure paths. `hardware_tests.exe` closes
that gap: it starts the **real `audior_gui.exe`**, records on **real devices**, and checks
the files that come out. It is native C++ like the rest of the project (Win32 and Media
Foundation only; no scripts, no third-party tools).

## Running

```bat
build.bat x64 hardware          :: also builds bin\x64\hardware_tests.exe (x64 and x86 without the "x64")
bin\x64\hardware_tests.exe
```

| Option | |
|---|---|
| `--only T1,T6` | Run only these tests. |
| `--min-seconds 32` | How long every recording lasts (default 32; never below 1). Each file then holds at least 30 s. |
| `--foreground` | Open the application windows normally instead of in the background. |
| `--no-sound` | Do not play the test tone; the loopback tests (T5 to T7, T9, T11) are skipped. |
| `--keep` | Keep the recordings (in a folder under `%TEMP%`) instead of deleting them at the end. |

The exit code is the number of failed checks (at most 100); 125 means the tests could
not be set up (no application next to the test program, no microphone or playback
device, a bad option). The whole run takes about 13 minutes.

**Needs:** a microphone and a playback device, a desktop session, and
`audior_gui.exe` in the same folder as `hardware_tests.exe` (`build.bat hardware`
puts them there). T12 also uses the other architecture's build if it is built.

### What it does to your PC

- **Records the default microphone** for the whole run (whatever it hears), in a scratch folder
  under `%TEMP%` that is deleted at the end.
- **Plays a quiet 440 Hz tone** on the default playback device: 5% of full scale (-26 dBFS), 2 s
  per recording, 12 s once. That is the signal the loopback checks look for.
- **Opens the application windows behind everything else, without taking the focus.** They are
  visible and fully usable (click one in the taskbar to watch it), but they never cover the window
  you are working in and the keyboard focus never moves. The program passes its launch
  show-command straight to `ShowWindow`, so the test program starts it with `SW_SHOWMINNOACTIVE`,
  restores it with `SW_SHOWNOACTIVATE` and sends it to the bottom of the window stack
  (`tests/hardware/app_process.cpp`). T14 samples the foreground window every 100 ms and fails
  if it ever belongs to the application; clicking a window yourself counts as stolen focus.
- **Bluetooth headsets:** opening the microphone switches the headset to its hands-free
  profile, so its audio quality drops for the duration and music may stutter. That is the
  headset, not the program.
- Other sounds on the PC during the run (notifications, music) land in the system recording
  and can fail the "silent after the tone" checks (T6, T9); the failure detail says so.

## The tests

Every recording below lasts at least 32 s. "Clock" is the time from the window appearing to
`--stop`; a file must match it within 1.5 s (2 s for T11).

| ID | Scenario | Checks |
|---|---|---|
| T1 | Microphone to MP3 | the window starts; `--stop` prints `Stop signal sent.`, exit 0; the window closes with exit 0; the file decodes as MP3 (Media Foundation), is 30 s+ long, matches the clock, runs at 128 kbit/s |
| T2 | Microphone to WAV | decodes as WAV, 30 s+, matches the clock, 48 kHz stereo, the header's sizes match the data |
| T3 | MP3 bit rates 96 and 320 | each file decodes, 30 s+, clock, bit rate within 8% |
| T4 | File-name collision | two takes of one name give `name.wav` and `name (2).wav` |
| T5 | Merged mic + system audio | one file only; decodes, 30 s+, clock; the 440 Hz tone is in it |
| T6 | Two separate tracks | `_mic` and `_system` files, each 30 s+ and on the clock; **same length within 0.15 s**; the tone is in the system track; the system track is silent after the tone (idle loopback is padded) |
| T7 | System gain, `--system-amplify 0` vs `300` | the tone's peak is about 12 dB louder (x4), measured only inside the tone's 0.8 s window |
| T8 | Microphone gain, `-a 0` vs `-a 300` | the mean level rises about 12 dB (skipped, with a note, if the microphone is digitally silent) |
| T9 | Choosing devices | microphone by **id** and playback device by **name** (tone captured); then up to two other playback devices by **index**: both tracks 30 s+, same length, and silent (the tone went elsewhere) |
| T10 | Bad devices, stray `--stop` | `-m 99` and `-i -d 99` exit 1 with a message and write nothing; `--stop` with no recording exits 1 with `No active recording to stop.` |
| T11 | Closing the window while recording | the window closes with exit 0 and both MP3 files are complete and 30 s+ |
| T12 | The other architecture's build | mic + system, 30 s+, clock, tone captured, tracks the same length |
| T13 | Resources over one 60 s recording | in the second half: private memory grows by less than 3 MB, handles by at most 3, threads by at most 2; CPU under 15% of a core; both MP3 files 30 s+ and the same length |
| T14 | Focus | the foreground window never belonged to the application during the run |

### How the files are measured

`tests/hardware/audio_probe.cpp` reads each file the way a listener would: WAV is parsed
chunk by chunk, MP3 is decoded by Media Foundation's source reader. Only the peak and the
energy of every tenth of a second are kept, so a long file costs a few KB. Levels are then
asked for over any stretch.

Why stretches rather than the whole file: the first version of the gain test measured
the whole-file maximum and reported that gain 300 was *quieter* than gain 0. The cause was
an unrelated system sound at about 20 s in the gain-0 file (-11.8 dB, not the tone). Measured
inside the tone's window the same files show -26.1 dB at gain 0 and -14.0 dB at gain 300:
exactly x4. The tone checks therefore look only at seconds 1.6 to 2.4 of the recording.

## Not automated

These need a person, so they stay on the checklist in [FIRST-TEST.md](FIRST-TEST.md): how the
window looks, the resting waveforms, the CLIP flag with a loud voice, unplugging a USB
microphone mid-recording, unticking **Microphone** (the command line always includes it), and
the check that other programs' volume does not drop while recording (the communications
ducking opt-out).

## Results on the reference PC

First complete run, 2026-10-11, x64 build, every recording 32 s (T13: 60 s).

| | |
|---|---|
| PC | Windows 11 Home 10.0.26300, one Bluetooth headset that is both the default microphone and the default playback device, plus two digital playback outputs (on-board S/PDIF, monitor audio) |
| Toolchain | Visual Studio 2022 Build Tools 17.14, MSVC 14.44, Windows SDK 10.0.26100 (see the README for the one-line install) |
| Unit tests | 215 of 215 pass, x64 and x86; the import check passes (only in-box DLLs) |
| Hardware tests | **100 checks: 95 pass, 5 fail** (see below); T14: the foreground window never belonged to the application in 5,732 samples |

The 100 checks are those of the first full run (94 passed, 6 failed). Its sixth failure was T13's
handle check, whose rule was then changed (see finding 2); T13 was re-run on its own with the final
rule and passes except for the length check, which is the same open finding. The T7, T8 and T10
tests, refactored after the full run, were re-run and pass.

What passed, on real hardware: MP3 and WAV from the microphone (48 kHz stereo, 128 kbit/s MP3,
96 and 320 kbit/s within 0.4%, WAV header consistent); every mic-only file within 0.3 s of the
clock; `name (2)` collision handling; merged recordings contain the system tone; system gain 300
is exactly +12.0 dB (x4) over gain 0; device selection by id, by name and by index (idle devices
write silence of the right length); bad devices and a stray `--stop` are rejected with the right
messages; closing the window mid-recording leaves two complete MP3s; the x86 build records
identically; over a minute the program uses 84 MB private memory (flat), about 1.4% of one core,
37 threads, and 656 to 658 handles after warm-up.

## Findings

### 1. OPEN: the system track runs longer than the clock and the microphone track

Five checks fail for the same reason. With `--separate` (or `--merge`, where the merged file
takes the longer length) the system track is longer than the microphone track and than the
time that actually passed, and the gap **grows with the recording**:

| Run | Clock | Mic file | System file | System minus mic |
|---|---|---|---|---|
| T6, x64 WAV | 32.09 s | 32.230 s | 32.802 s | +0.572 s |
| T9 device [1] (nothing plays on it) | 32.09 s | 32.230 s | 32.791 s | +0.561 s |
| T9 device [2] (nothing plays on it) | 32.09 s | 32.250 s | 32.815 s | +0.565 s |
| T12, x86 WAV | 32.09 s | 32.660 s | 33.227 s | +0.567 s |
| T13, MP3 | 32.02 s | 32.293 s | 32.869 s | +0.576 s |
| T13, MP3, 62 s | 62.00 s | 62.245 s | 63.253 s | +1.008 s |
| T13, MP3, 60 s | 60.03 s | 60.253 s | 61.333 s | +1.080 s |

- The microphone is fine: it is 0.1 to 0.3 s over the clock (the recording starts just before the
  window is seen), exactly as in the mic-only runs (T1, T2), and does not drift.
- The system track is 0.7 s over the clock at 32 s and 1.25 to 1.3 s over at 60 s.
- It does not depend on what is played (devices that carry no audio show it), on the format (WAV
  and MP3) or on the architecture (x64 and x86).
- `SilenceFiller` itself is not the cause: driven alone with a simulated clock and a device that
  delivers nothing, it writes 31.960 s for 32 s and 61.970 s for 62 s.

Effect: in a merged file the microphone ends up early against the system audio by an amount that
grows with the recording (about 0.6 s after half a minute, about 1 s after a minute); separate
tracks differ in length and cannot be laid side by side without drift. Where to look next:
how the loopback stream's frames and padding are counted from `WasapiCapture::Poll`
(`wasapi_capture.cpp`) through `RecordingSession::Deliver` and `Drain` (`recording_session.cpp`),
and whether the two streams start from a common origin. A fix needs a regression test with a
fake clock and a source that delivers nothing, which must produce the clock's length within 50 ms.
Not fixed in this change; `hardware_tests.exe` keeps reporting it (the "same length" checks).

### 2. The handle count rises during warm-up, then holds (test design, fixed)

The first rule compared the end of a recording with its 5 s mark and failed (652 to 658 handles).
The full series (646 at start, 651 at 5 s, 656 at 30 s, 656 to 658 until 62 s, memory +0.1 MB over
the last 57 s, threads 34 to 38) shows Media Foundation, COM and Direct2D taking what they need in
the first half minute and then holding steady. T13 now records at least 60 s and compares the
second half with the point half way; it passes.

### 3. Microphone gain cannot be measured exactly with an uncontrolled input

T8 passes its 8 to 16 dB window with +8.0 and +8.4 dB on two runs where x4 would be +12 dB. The
input is whatever the room delivers, which differs between the takes and sits near the headset's
noise floor, so this is not evidence against the gain code (the deterministic system-gain test
gives exactly +12.0 dB, and the unit tests cover the arithmetic). A tight test needs a known
acoustic source.

### 4. Measure levels over the stretch that matters (test design, fixed)

See "How the files are measured": a whole-file maximum is spoiled by any unrelated system sound.

### 5. How the harness stays out of the way, and a command-line detail

- A window started minimized and not activated, then restored with `SW_SHOWNOACTIVATE` and sent
  to the bottom of the stack, stays visible and never takes the focus (0 of 5,732 foreground
  samples), and `PostMessage(WM_CLOSE)` closes it exactly as the close button does.
- A bad `-m` or `-d` value is rejected by the command line before any window opens (exit 1).
