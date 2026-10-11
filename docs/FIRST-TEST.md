# First test on real hardware

The unit tests cover everything after the capture and the failure paths of the
capture, but the build machines have no microphone. The checks below are the part
only a PC with audio hardware can do.

## Run the automated hardware tests first

`hardware_tests.exe` does most of this list for you, on your devices, and checks the
files it records (length, format, bit rate, levels, track alignment, gain, device
selection, memory and CPU). Build and run it; it takes about 12 minutes and every
recording lasts at least 30 s:

```bat
build.bat x64 hardware
bin\x64\hardware_tests.exe
```

It plays a quiet test tone on the default playback device, records the microphone,
and keeps the application windows behind your other windows without taking the
focus. See [HARDWARE-TESTS.md](HARDWARE-TESTS.md) for what each test checks, the
results on the reference PC and the findings. Covered there: MP3 and WAV from the
microphone (section 1, steps 3 and 4, and the WAV row), merged and separate
recordings with silence kept in step, both gains, naming and collisions, device
choice by id, name and index, closing the window while recording, the other
architecture, the command-line modes (`--list-devices`, `--stop`, bad options).

Still manual, because they need a person or a look at the screen: section 1 steps 1
and 2 (the lists and the resting waveforms), watching the waveforms move, the CLIP
flag with a loud voice, unplugging the USB microphone while recording, unticking
**Microphone**, and the check that other programs' volume does not drop.

The rest of this page is the manual version of everything.

## Get the program

Either build it (`build.bat x64`), or download **audior-gui-windows** from the
*Artifacts* of the latest green run of the CI workflow and unzip it. Run
`Audior-GUI.exe`; there is nothing to install.

If Windows SmartScreen warns about an unknown publisher (the file is not code
signed), choose *More info*, then *Run anyway*.

## 1. The window

1. Start `Audior-GUI.exe`. Both drop-downs should list your devices, each marked
   *Default · name* at the top.
2. While idle the waveforms rest; they move only while recording.
3. Press **Start recording**, speak, play some music, press **Stop**.
   The waveforms move, the time runs, the status says *Saved*, **Show in folder**
   opens `Documents\Audior-GUI`.
4. Play the file. For MP3, check the length matches the clock.

## 2. The cases worth trying

| Try | Expect |
|---|---|
| Tick **System audio**, play a video, record merged | One file with both. Move the *Share of the mix* slider and record again: the balance changes. |
| Same, **Two separate tracks** | `..._mic` and `..._system` files, equal length. Pause the music for a while: the system file still has the full length (silence) and stays in step. |
| Microphone gain 300%, speak loudly | The **CLIP** flag lights; the file is louder. |
| System audio gain 150% | The system waveform is taller and the file louder. |
| Untick **Microphone**, keep **System audio** | Only the system file/lane. |
| Record with a Bluetooth headset's microphone while music plays | A note under the microphone drop-down says the headset switches to call mode. While recording the headset's output drops out for about half a second and its volume and sound change; both return when you stop. With only **System audio** ticked nothing changes. Click **Record system audio only** in the note: Microphone unticks, System audio ticks and the note goes. |
| Type a **File name** (`standup`) | `standup.mp3`; record again: `standup (2).mp3`. |
| Switch **Format** to WAV | A `.wav` file, 48 kHz 16-bit stereo. |
| Unplug the USB microphone *while recording* | Recording stops at once with *Audio capture failed: the device was unplugged...*; the file keeps what was recorded. |
| Close the window while recording | The file is complete and plays. |

## 3. The tray and the hotkeys

These are done by hand: the build machines have no desktop shell to check them on.

| Try | Expect |
|---|---|
| Minimize the window | It leaves the taskbar and an Audior-GUI icon (a ring) appears in the notification area, possibly under the `^` overflow arrow. |
| Click the icon | The window comes back and the icon goes. |
| Press **Ctrl+Alt+R** with the window in the tray, then look at the icon | A red dot, and the tooltip shows `Audior-GUI · recording 00:00:05` counting up. |
| Press **Ctrl+Alt+S** | The icon returns to a ring. Restore the window: *Saved*, and the file is complete. |
| Right-click the icon | *Show Audior-GUI*, *Start recording*, *Stop*, *Exit*, with the hotkeys shown. Only the one that applies is enabled. *Exit* while recording leaves a complete file. |
| Press **Ctrl+Alt+R** in another program (Notepad, a browser) | It starts a recording there too, and Ctrl+S and Ctrl+R still work in that program. |
| Unplug the microphone while recording with the window in the tray | A balloon names the problem. |
| In *Hotkeys*, click **Start**, press **Ctrl+Alt+M** | The box shows `Ctrl+Alt+M`; Ctrl+Alt+R no longer starts a recording and Ctrl+Alt+M does. Close and reopen the program: it is still `Ctrl+Alt+M`. |
| Click **Start**, press **R** alone | Refused: *Add Ctrl or Alt*. The box keeps listening; **Esc** gives up. |
| Choose the other box's shortcut | Refused: *... is already the Stop shortcut*. |
| Run a second copy | Its hotkey boxes are outlined in red: the first copy owns the shortcuts. |
| Start with a shortcut set to *Run: Minimized* | The window never shows; the icon is in the tray. |

The saved shortcuts are two text values in `HKEY_CURRENT_USER\Software\Audior-GUI`
(`StartHotkey`, `StopHotkey`); delete the key to go back to Ctrl+Alt+R and Ctrl+Alt+S.

## 4. From a command prompt

```bat
Audior-GUI --list-devices
Audior-GUI -o "%TEMP%\cli-test" -f wav -i --separate     :: the window opens already recording
Audior-GUI --stop                                         :: from another prompt: ends it
```

`Audior-GUI --stop` prints `Stop signal sent.`; with nothing recording it prints
`No active recording to stop.` and exits with code 1. While other programs play
audio, their volume should not drop when you start recording (the communications
ducking opt-out).

## If something is wrong

Note the message in the window, including the `hr=0x...` code, and the output of
`Audior-GUI --list-devices`.
