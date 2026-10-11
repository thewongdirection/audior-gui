# Hardware tests

CI runners have no audio devices, so the capture path (`wasapi_capture.cpp`,
`devices.cpp`) is only unit-tested for its failure paths. `hardware_tests.exe` closes
that gap: it starts the **real `Audior-GUI.exe`**, records on **real devices**, and checks
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
`Audior-GUI.exe` in the same folder as `hardware_tests.exe` (`build.bat hardware`
puts them there). T12 also uses the other architecture's build if it is built.

### What it does to your PC

- **Records the default microphone** for the whole run (whatever it hears), in a scratch folder
  under `%TEMP%` that is deleted at the end.
- **Plays a quiet 440 Hz tone** on the default playback device: 5% of full scale (-26 dBFS), 2 s
  per recording, 12 s once. That is the signal the loopback checks look for.
- **Opens the application windows behind everything else, without taking the focus.** They are
  visible and fully usable (click one in the taskbar to watch it), but they never cover the window
  you are working in and the keyboard focus never moves. The program passes its launch
  show-command straight to `ShowWindow`, so the test program starts it with `SW_SHOWMINNOACTIVE`
  (which sends a window to the tray, hidden: the test program finds it by its window class, not
  by visibility), restores it with `SW_SHOWNOACTIVATE` and sends it to the bottom of the window stack
  (`tests/hardware/app_process.cpp`). T14 samples the foreground window every 100 ms and fails
  if it ever belongs to the application; clicking a window yourself counts as stolen focus. A
  failure says when it happened and how it began, for example `308 of 5885 samples, in 2
  episodes: T1 at 3.4 s for 29.6 s (no input for 6.1 s: not a click, something else brought it
  forward); T12 at 25.0 s for 8.1 s (input 0.2 s earlier: probably someone clicking or typing)`.
  The cause is read from how long ago the last keyboard or mouse input was when the window came
  forward, and whether a mouse button was down (`tests/hardware/focus_log.h`, unit-tested in
  `tests/test_focus_log.cpp`). Input another program injects, such as a UI-automation session,
  counts as input, so it reads as "someone".
- **Nothing else may record meanwhile.** Every `Audior-GUI.exe` ends on the same machine-wide stop
  event (`Local\AudiorStopEvent`) and the microphone is shared, so a second instance recording or
  sending `--stop` cuts these recordings short. The tests refuse to start while another
  `Audior-GUI.exe` (or a build from before the rename, `audior_gui.exe`) or `hardware_tests.exe` is running and T14 notes any that appears during the
  run (short `--demo` or screenshot runs are harmless).
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
| T14 | Focus | the foreground window never belonged to the application during the run; any other `Audior-GUI.exe` seen during the run is noted (they share the stop signal) |

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

Two complete runs on 2026-10-11, x64 build, every recording 32 s (T13: 60 s), before and after
the fix described in finding 1.

| | |
|---|---|
| PC | Windows 11 Home 10.0.26300, one Bluetooth headset that is both the default microphone and the default playback device, plus two digital playback outputs (on-board S/PDIF, monitor audio) |
| Toolchain | Visual Studio 2022 Build Tools 17.14, MSVC 14.44, Windows SDK 10.0.26100 (see the README for the one-line install) |
| Unit tests | before the fix 215 of 215, after it 221 of 221 (six new); x64 and x86; the import check passes (only in-box DLLs) |
| Hardware tests, before the fix | 100 checks: 95 pass, 5 fail (the five "same length" checks) |
| Hardware tests, after the fix | **100 checks: all pass**; the foreground window never belonged to the application in 5,951 samples |

The "before" run's T13 handle check had been changed after the first attempt (finding 2) and
its figures count T13 under the final rule. The "after" run also noted 13 other `Audior-GUI.exe`
processes during its 14 minutes, short runs by other sessions on the same PC; it then counted
that as a failed check (101 checks, 100 passed), which was changed to a note because none of
them touched a recording (every other check passed).

What passed, on real hardware: MP3 and WAV from the microphone (48 kHz stereo, 128 kbit/s MP3,
96 and 320 kbit/s within 0.4%, WAV header consistent); every mic-only file within 0.3 s of the
clock; `name (2)` collision handling; merged recordings contain the system tone; system gain 300
is exactly +12.0 dB (x4) over gain 0; device selection by id, by name and by index (idle devices
write silence of the right length); bad devices and a stray `--stop` are rejected with the right
messages; closing the window mid-recording leaves two complete MP3s; the x86 build records
identically; over a minute the program uses 84 MB private memory (flat), about 1.4% of one core,
37 threads, and 656 to 658 handles after warm-up.

## Findings

### 1. FIXED: the streams started out of step (system track longer than the mic track)

**Symptom.** With `--separate` the system track was 0.56 to 0.58 s longer than the microphone
track in every 32 s run, and 1.0 to 1.13 s longer in the 60 s runs; with `--merge` the
microphone ended up that much early against the system audio.

| Run | System minus mic, before | after |
|---|---|---|
| T6, x64 WAV | +0.572 s | -0.010 s |
| T9 device [1] (nothing plays on it) | +0.561 s | +0.006 s |
| T9 device [2] (nothing plays on it) | +0.565 s | -0.017 s |
| T12, x86 WAV | +0.567 s | +0.006 s |
| T13, MP3, 32 s / 60 s | +0.576 s / +1.008 to +1.128 s | -0.024 s (60 s) |

Mic-only recordings are unchanged: 32.27 s of file for a 32.06 s clock (T1).

**A wrong turn, corrected.** The first reading was that the gap grows with the recording. It
does not: the two 60 s runs simply met a slower start (see below). What decided it: `WasapiCapture`
on its own, polled like the session does, is within 0.05 s of the clock for both the microphone
and the loopback; a kept 60 s recording showed the 12 s test tone exactly 12.000 s long (no
stretching) starting 2.333 s into the system file although it was played 1 s after the test's
clock began, and the stretch from the tone's end to the end of the file matched real time
(47.08 s). So the system file starts earlier than the microphone file.

**Cause.** `RecordingSession` starts every stream on its own thread, and each `WasapiCapture`
measured time from its own `Start()`. On this headset the microphone's `Start()` blocks 610 to
720 ms (the headset switches to its hands-free profile) while the loopback's takes 0 ms, so the
loopback ran about 0.6 s, sometimes over a second, before the microphone delivered anything.
Only the loopback was ever padded to the clock, so nothing made the microphone catch up.
`SilenceFiller` itself was correct (31.960 s for 32 s with a simulated clock).

**Fix** (`src/engine/session_clock.h`, `silence_filler.h`, `wasapi_capture.*`,
`wasapi_engine.cpp`):

- The streams of a recording share one `SessionClock`. Its origin is the moment the first stream's
  device is *running* (each stream claims it as `client_->Start()` returns). Claiming before
  `Start()` was tried first and gave a mic-only recording the microphone's whole start-up time as
  leading silence (T1: 33.8 s for a 32 s clock); a lone stream is now first, so it gets no gap.
- Every stream, microphone included, pads itself to that clock. The silence goes in *front of* the
  audio of the poll that found the gap, since the missing time came first.
- The first gap may be up to 30 s (it is the time the device took to start); later gaps keep the 2 s
  clock-jump limit.
- Six regression tests in `tests/test_sinks.cpp`, among them the scenario above (a loopback that
  starts at once and a microphone that takes 650 ms, polled for 5 s: both tracks end within 50 ms of
  each other and the microphone starts with 0.64 s of silence).

**Known limits.** A microphone whose clock runs faster than the PC's is not trimmed, so very long
recordings could still show a small difference; a microphone more than 50 ms behind the clock is
padded in steps (a guard that was never needed in these runs).

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

### 6. Recording with a Bluetooth headset's microphone switches the headset to call mode (not a bug here; the window now says so)

**Report.** With a Bluetooth headset as both the default speaker and microphone, music playing,
and **System audio** ticked, pressing **Start recording** made the audio "jump from the
headset to the monitor"; it jumped back on **Stop**.

**What it is.** A Bluetooth headset plays music in its stereo mode. When anything opens its
microphone, Windows switches it to the hands-free (phone call) mode, and back when the microphone
is closed. The program does not choose this and cannot avoid it while the microphone is open.

**How it was pinned down.** A throw-away probe (not in the repository) took a snapshot every
200 ms of: the default playback and recording devices for all three roles, every playback and
recording endpoint's state, master volume, mute flag and level meter, and the audio sessions on
each endpoint. The same music (Chrome and a quiet test tone) played throughout.

| Recording | Headset output volume | Output | Volume after Stop |
|---|---|---|---|
| Microphone and system audio (the report, from the window) | 40 to 45 at the start | silent for 0.4 s, then sound again | back to 40, 5 s after Stop |
| Microphone only (command line) | 40 to 45 at the start | silent for 0.4 s, then sound again | back to 40, 5 s after Stop |
| System audio only, microphone unticked (window) | unchanged | unchanged | unchanged |

- Opening the microphone is what triggers it: the step appears with the microphone alone and not
  with the system audio alone. The system audio capture is a loopback of the headset's playback
  side and does not touch the headset's microphone side.
- **Windows did not re-route anything.** In every run the default playback device stayed the
  headset, Chrome's and the test tone's streams stayed on it, no stream ever appeared on the
  monitor's endpoint, and the monitor's level meter stayed at silence.
- **Not explained:** the impression that the sound moved to the monitor. The logs show the
  headset still carrying the sound and nothing on the monitor; if it persists, note which sound
  came out of which speaker and when.

**What the program does about it.** `src/core/bluetooth.h` and the device list: each device
carries a `bluetooth` flag, read from the bus Windows reports for its endpoint (`BTHHFENUM` for a
headset's microphone, `BTHENUM` for its playback side, `HDAUDIO` for the PC's own and the monitor's;
anything starting `BTH` counts). When the microphone that would be recorded is Bluetooth, the
microphone pane shows *Bluetooth microphone: the headset switches to call mode while recording.*
under its drop-down, with a **Record system audio only** link on the same line that unticks the
microphone and ticks system audio in one click (`UseSystemAudioOnly`, which also switches the system
audio's **Transcript** on when the microphone's is on; hidden while recording, because the settings
are locked then). Five unit tests cover the detection and the choice of microphone (default or a
chosen one), two the link's settings change, and the `bluetooth-mic`
screenshot scenario renders the note and the link (also in CI). Checked by clicking the link in the
real window on the real hardware: the microphone unticked, system audio ticked, the note went.

**What avoids it.** Record **System audio only** (untick **Microphone**), or use a microphone that
is not Bluetooth (a USB or wired one) and keep the headset only for listening.
