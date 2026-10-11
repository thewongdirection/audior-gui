/*
 * hardware_tests.cpp - Records with the real Audior-GUI.exe on this PC's real audio hardware
 * and checks the files it writes.
 *
 *     hardware_tests.exe [--only T1,T6] [--min-seconds 32] [--foreground] [--no-sound] [--keep]
 *
 * Needs a microphone, a playback device and a desktop session.  Every recording lasts at least
 * 30 seconds (--min-seconds, 32 by default), so every file holds that much audio; the whole run
 * takes about 12 minutes.  The exit code is the number of failed checks (at most 100); 125 means
 * the tests could not be set up (no application, no devices, a bad option).
 *
 *   * The microphone records whatever it hears.  For the loopback checks the program plays a
 *     quiet test tone (440 Hz, -26 dBFS, 2 s per recording, 12 s once) on the default playback
 *     device; --no-sound skips those checks instead.
 *   * The application windows open visibly but behind every other window, without taking the
 *     keyboard focus (see app_process.h), so the PC stays usable and a window can be brought up
 *     from the taskbar to watch.  --foreground opens them normally.  T14 counts how often the
 *     foreground window belonged to the application: clicking one yourself shows up there.  When it
 *     fails it says when, for how long, and how long after the last keyboard or mouse input
 *     (focus_log.h), which tells a click from a window that came forward by itself.
 *   * Nothing else may use the program, the microphone or the stop signal meanwhile: every
 *     instance of Audior-GUI.exe ends on the same machine-wide stop event, so a second one (or a
 *     second run of these tests) cuts the recordings short.  The tests refuse to start while
 *     another instance runs, and T14 notes any that appears during the run (a short --demo or
 *     screenshot run is harmless; a recording or a --stop is not, and shows in the checks).
 *   * The recordings go to a scratch folder under %TEMP% that is removed at the end (--keep
 *     leaves it).
 *
 * T1 MP3 and T2 WAV from the microphone, T3 MP3 bit rates, T4 file-name collisions, T5 merged
 * mic + system audio, T6 separate tracks and the padding that keeps them in step, T7 system
 * gain, T8 microphone gain, T9 choosing devices by id, name and index, T10 bad devices and a
 * stray --stop, T11 closing the window while recording, T12 the other architecture's build,
 * T13 memory, handles, threads and CPU over a recording, T14 the focus, T15 the live transcript
 * of a spoken sentence (the one test that runs the recording engine itself, in this process;
 * skipped unless the transcription helper and a model are in place), T16 a busy PC (the
 * transcript changes to a lighter model) and T17 the large model on the graphics card at the
 * pace of speech (both also need the large model, and no audio devices), and T18 Parakeet at the
 * pace of speech, on the processor and on the card (skipped without a Parakeet model).
 *
 * Not automatable (docs\FIRST-TEST.md): unplugging a device, a loud voice for the CLIP flag,
 * the communications-ducking opt-out, and how the window looks.
 */
#include <windows.h>

#include <mfapi.h>
#include <mmsystem.h>
#include <sapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../../src/engine/wasapi_engine.h"
#include "../temp_dir.h"
#include "app_process.h"
#include "audio_probe.h"
#include "focus_log.h"

namespace {

/* ---- options and reporting ------------------------------------------------ */

struct Options {
    double                   minSeconds = 32.0;
    bool                     foreground = false;
    bool                     noSound = false;
    bool                     keep = false;
    std::vector<std::string> only;
} g_options;

constexpr int kSetupError = 125;

struct Outcome {
    std::string section, what, detail;
    bool        ok;
};

std::vector<Outcome> g_outcomes;
std::string          g_section;
FocusLog             g_focus;                           /* when the application's window held the foreground (T14) */
std::vector<DWORD>   g_strangers;                       /* other copies of the program seen while recording */

std::string Fmt(const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    return text;
}

std::string Narrow(const std::wstring &w)
{
    if (w.empty())
        return {};
    std::string s(static_cast<std::size_t>(WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), static_cast<int>(s.size()), nullptr, nullptr);
    return s;
}

std::wstring Widen(const std::string &s)
{
    if (s.empty())
        return {};
    std::wstring w(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), static_cast<int>(w.size()));
    return w;
}

std::wstring Quote(const std::wstring &s) { return L"\"" + s + L"\""; }

bool Wanted(const char *id)
{
    if (g_options.only.empty())
        return true;
    for (const std::string &o : g_options.only) {
        if (o == id)
            return true;
    }
    return false;
}

void Section(const char *title)
{
    g_section = title;
    std::printf("\n== %s\n", title);
}

void Check(const std::string &what, bool ok, const std::string &detail = {})
{
    std::printf("  %s  %s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : ("  [" + detail + "]").c_str());
    g_outcomes.push_back({g_section, what, detail, ok});
}

void Info(const std::string &text) { std::printf("  info  %s\n", text.c_str()); }

/* ---- the machine ---------------------------------------------------------- */

struct Device {
    int         index;
    std::string name;
    std::string id;
    bool        isDefault;
};

std::wstring           g_exe, g_otherExe;
std::vector<Device>    g_mics, g_speakers;
std::unique_ptr<TempDir> g_dir;
std::wstring           g_tone2, g_tone12;
bool                   g_loopback = true;

std::wstring Path(const wchar_t *name) { return g_dir->Path(name); }

/* The devices from the program's own --list-devices output. */
void ReadDevices(const std::string &text)
{
    std::vector<Device> *list = nullptr;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();

        static const std::string kDefaultMark = "  [DEFAULT]";
        const std::size_t open = line.find_first_not_of(' ');
        if (line.rfind("Microphones", 0) == 0) {
            list = &g_mics;
        } else if (line.rfind("Speakers", 0) == 0) {
            list = &g_speakers;
        } else if (list && open != std::string::npos && line[open] == '[') {        /* "  [0] Name  [DEFAULT]" */
            std::string name = line.substr(line.find(']') + 1);
            name.erase(0, name.find_first_not_of(' '));
            const bool isDefault = name.size() > kDefaultMark.size() &&
                                   name.compare(name.size() - kDefaultMark.size(), kDefaultMark.size(), kDefaultMark) == 0;
            if (isDefault)
                name.erase(name.size() - kDefaultMark.size());
            list->push_back({std::atoi(line.c_str() + open + 1), name, {}, isDefault});
        } else if (list && !list->empty() && line.find("ID: ") != std::string::npos) {
            list->back().id = line.substr(line.find("ID: ") + 4);
        }
    }
}

const Device &DefaultOf(const std::vector<Device> &devices)
{
    for (const Device &d : devices) {
        if (d.isDefault)
            return d;
    }
    return devices.front();
}

/* ---- the test tone -------------------------------------------------------- */

void Put16(std::uint8_t *p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); }
void Put32(std::uint8_t *p, std::uint32_t v) { Put16(p, static_cast<std::uint16_t>(v)); Put16(p + 2, static_cast<std::uint16_t>(v >> 16)); }

/* A mono 48 kHz 16-bit sine, 440 Hz at 5% of full scale (-26 dBFS): audible but not loud. */
bool WriteTone(const std::wstring &path, int seconds)
{
    constexpr int    kRate = 48000;
    constexpr double kAmplitude = 0.05, kFrequency = 440.0, kPi = 3.14159265358979323846;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(seconds) * kRate * 2;
    std::uint8_t header[44];
    std::memcpy(header, "RIFF", 4);
    Put32(header + 4, 36 + dataBytes);
    std::memcpy(header + 8, "WAVEfmt ", 8);
    Put32(header + 16, 16);
    Put16(header + 20, 1);                              /* PCM */
    Put16(header + 22, 1);                              /* mono */
    Put32(header + 24, kRate);
    Put32(header + 28, kRate * 2);
    Put16(header + 32, 2);
    Put16(header + 34, 16);
    std::memcpy(header + 36, "data", 4);
    Put32(header + 40, dataBytes);

    bool ok = true;
    DWORD written = 0;
    ok = WriteFile(file, header, sizeof header, &written, nullptr) != FALSE;
    std::vector<std::int16_t> oneSecond(kRate);
    for (int s = 0; s < seconds && ok; ++s) {
        for (int i = 0; i < kRate; ++i)
            oneSecond[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(kAmplitude * 32767.0 * std::sin(2.0 * kPi * kFrequency * i / kRate));
        ok = WriteFile(file, oneSecond.data(), static_cast<DWORD>(oneSecond.size() * 2), &written, nullptr) != FALSE;
    }
    CloseHandle(file);
    return ok;
}

struct Tone {
    double              at;                             /* seconds into the recording */
    const std::wstring *wav;
};

std::vector<Tone> OneTone()
{
    return g_loopback ? std::vector<Tone>{{1.0, &g_tone2}} : std::vector<Tone>{};
}

/* ---- one recording -------------------------------------------------------- */

struct Take {
    bool                         started = false;       /* the program ran and showed its window */
    CliResult                    stop;                  /* what --stop said */
    double                       elapsed = 0.0;         /* seconds from the window to --stop */
    int                          exitCode = -1;         /* of the window once closed */
    std::vector<AppProcess::Usage> usage;               /* every 5 s, when asked */
    std::vector<double>          usageAt;               /* seconds into the recording of each sample */
};

/* The program's file name, and the one it had before the rename: a copy built earlier shares its stop signal too. */
const wchar_t *const kExeNames[] = {L"Audior-GUI.exe", L"audior_gui.exe"};

/* Remembers any copy of the program other than `own` that is running: it would share the stop signal. */
void NoteStrangers(DWORD own)
{
    for (const wchar_t *name : kExeNames) {
        for (const DWORD id : FindProcesses(name)) {
            bool known = id == own;
            for (const DWORD seen : g_strangers)
                known = known || seen == id;
            if (!known)
                g_strangers.push_back(id);
        }
    }
}

/* Starts the program, plays the tones, stops it with --stop after the minimum recording time (or
 * `seconds`, if longer) or, with `closeInstead`, by closing its window, and closes the window.  It runs
 * with --defaults: audior's defaults are what a take starts from, whatever the last real run saved, and
 * the settings of whoever runs the tests are left as they were. */
Take Record(const std::wstring &exe, const std::wstring &args, const std::vector<Tone> &tones = {}, bool closeInstead = false,
            bool sampleUsage = false, double seconds = 0.0)
{
    const double length = seconds > g_options.minSeconds ? seconds : g_options.minSeconds;
    Take take;
    AppProcess app;
    if (!app.Start(exe, L"--defaults " + args, !g_options.foreground) || !app.HasWindow())
        return take;
    take.started = true;

    const ULONGLONG begin = GetTickCount64();
    std::size_t nextTone = 0;
    double nextSample = 0.0;
    for (;;) {
        const double now = static_cast<double>(GetTickCount64() - begin) / 1000.0;
        if (now >= length)
            break;
        if (nextTone < tones.size() && now >= tones[nextTone].at) {
            PlaySoundW(tones[nextTone].wav->c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
            ++nextTone;
        }
        if (sampleUsage && now >= nextSample) {
            take.usage.push_back(app.Sample());
            take.usageAt.push_back(now);
            nextSample += 5.0;
        }
        g_focus.Sample(g_section.substr(0, g_section.find(' ')), now, ForegroundProcessId() == app.Id(), MillisecondsSinceInput(),
                       MouseButtonDown());
        if (g_focus.Samples() % 10 == 0)
            NoteStrangers(app.Id());
        Sleep(100);
    }
    g_focus.EndRecording();
    take.elapsed = static_cast<double>(GetTickCount64() - begin) / 1000.0;
    if (sampleUsage) {
        take.usage.push_back(app.Sample());
        take.usageAt.push_back(take.elapsed);
    }
    if (!closeInstead) {
        take.stop = RunCli(exe, L"--stop");
        Sleep(1500);                                    /* the files are finalised */
    }
    PlaySoundW(nullptr, nullptr, 0);
    take.exitCode = app.CloseWindow();
    return take;
}

/* ---- checking files ------------------------------------------------------- */

std::unique_ptr<AudioProbe> CheckAudio(const std::string &label, const std::wstring &path, double elapsed, const char *format,
                                       double slack = 1.5)
{
    const bool exists = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    Check(label + " file exists", exists, Narrow(path.substr(path.rfind(L'\\') + 1)));
    if (!exists)
        return nullptr;
    auto probe = std::make_unique<AudioProbe>(path);
    Check(label + " decodes as " + format, probe->Valid() && probe->Format() == format);
    if (!probe->Valid())
        return nullptr;
    Check(label + " holds at least 30 s of audio", probe->Seconds() >= 30.0, Fmt("%.2f s", probe->Seconds()));
    Check(label + " length matches the clock", std::fabs(probe->Seconds() - elapsed) <= slack,
          Fmt("%.2f s file vs %.2f s elapsed", probe->Seconds(), elapsed));
    return probe;
}

void CheckSameLength(const std::string &label, const AudioProbe &mic, const AudioProbe &system)
{
    const double difference = system.Seconds() - mic.Seconds();
    Check(label + ": both tracks are the same length", std::fabs(difference) < 0.15,
          Fmt("mic %.3f s vs system %.3f s, difference %+.3f s", mic.Seconds(), system.Seconds(), difference));
}

/* The tone starts about a second into the recording and lasts 2 s: this stretch is inside it. */
double ToneDb(const AudioProbe &p) { return p.PeakDb(1.6, 0.8); }

/* ---- the tests ------------------------------------------------------------ */

void T1()
{
    Section("T1  microphone -> MP3");
    const std::wstring base = Path(L"t1");
    const Take t = Record(g_exe, L"-o " + Quote(base) + L" -f mp3");
    Check("the window started", t.started);
    Check("--stop reports success", t.stop.exitCode == 0 && t.stop.output.find("Stop signal sent") != std::string::npos,
          Fmt("code %d: %s", t.stop.exitCode, t.stop.output.c_str()));
    Check("the window closed with exit code 0", t.exitCode == 0, Fmt("exit %d", t.exitCode));
    if (auto p = CheckAudio("MP3", base + L".mp3", t.elapsed, "mp3")) {
        Check("MP3 is 128 kbit/s (the default)", std::fabs(p->Kbps() - 128.0) < 8.0, Fmt("%.1f kbit/s", p->Kbps()));
        Info(Fmt("%d Hz, %d ch", p->SampleRate(), p->Channels()));
    }
}

void T2()
{
    Section("T2  microphone -> WAV");
    const std::wstring base = Path(L"t2");
    const Take t = Record(g_exe, L"-o " + Quote(base) + L" -f wav");
    Check("--stop reports success", t.stop.exitCode == 0, Fmt("code %d", t.stop.exitCode));
    if (auto p = CheckAudio("WAV", base + L".wav", t.elapsed, "wav")) {
        Check("WAV is 48 kHz stereo", p->SampleRate() == 48000 && p->Channels() == 2, Fmt("%d Hz, %d ch", p->SampleRate(), p->Channels()));
        Check("the WAV header matches the data", p->HeaderMatchesData());
    }
}

void T3()
{
    Section("T3  MP3 bit rates");
    for (const int kbps : {96, 320}) {
        const std::wstring base = Path((L"t3_" + std::to_wstring(kbps)).c_str());
        const Take t = Record(g_exe, L"-o " + Quote(base) + L" -f mp3 -b " + std::to_wstring(kbps));
        if (auto p = CheckAudio(Fmt("MP3 @%d", kbps), base + L".mp3", t.elapsed, "mp3"))
            Check(Fmt("MP3 @%d has that bit rate", kbps), std::fabs(p->Kbps() - kbps) < kbps * 0.08, Fmt("%.1f kbit/s", p->Kbps()));
    }
}

void T4()
{
    Section("T4  file name collision");
    const std::wstring base = Path(L"named");
    Record(g_exe, L"-o " + Quote(base) + L" -f wav");
    Record(g_exe, L"-o " + Quote(base) + L" -f wav");
    Check("the first take is named.wav", GetFileAttributesW((base + L".wav").c_str()) != INVALID_FILE_ATTRIBUTES);
    Check("the second take is \"named (2).wav\"", GetFileAttributesW((base + L" (2).wav").c_str()) != INVALID_FILE_ATTRIBUTES);
}

void T5()
{
    Section("T5  merged mic + system audio");
    const std::wstring base = Path(L"t5");
    const Take t = Record(g_exe, L"-o " + Quote(base) + L" -f wav -i --merge 50", OneTone());
    Check("--stop reports success", t.stop.exitCode == 0, Fmt("code %d", t.stop.exitCode));
    if (auto p = CheckAudio("merged WAV", base + L".wav", t.elapsed, "wav"))
        Check("the merged file contains the system tone", ToneDb(*p) > -50.0, Fmt("tone window peak %.1f dB", ToneDb(*p)));
    Check("merging writes one file only", GetFileAttributesW((base + L"_mic.wav").c_str()) == INVALID_FILE_ATTRIBUTES &&
                                              GetFileAttributesW((base + L"_system.wav").c_str()) == INVALID_FILE_ATTRIBUTES);
}

void T6()
{
    Section("T6  two separate tracks, silence kept in step");
    const std::wstring base = Path(L"t6");
    const Take t = Record(g_exe, L"-o " + Quote(base) + L" -f wav -i --separate", OneTone());
    Check("--stop reports success", t.stop.exitCode == 0, Fmt("code %d", t.stop.exitCode));
    auto mic = CheckAudio("mic track", base + L"_mic.wav", t.elapsed, "wav");
    auto sys = CheckAudio("system track", base + L"_system.wav", t.elapsed, "wav");
    if (mic && sys) {
        CheckSameLength("separate", *mic, *sys);
        Check("the system track captured the tone", ToneDb(*sys) > -40.0, Fmt("tone window peak %.1f dB", ToneDb(*sys)));
        const double tail = sys->PeakDb(6.0, sys->Seconds() - 6.0);
        Check("the system track is silent after the tone (padded)", tail < -60.0, Fmt("tail peak %.1f dB; other system sounds during the run would show here", tail));
    }
}

/* Records twice, with the gain option at 0 and at 300 (x1 and x4), and measures each file. */
struct GainPair {
    bool   ok = true;
    double at0 = 0.0, at300 = 0.0;
};

GainPair RecordGains(const wchar_t *stem, const std::wstring &options, const std::wstring &gainOption, const wchar_t *fileSuffix,
                     const std::vector<Tone> &tones, double (*measure)(const AudioProbe &))
{
    GainPair pair;
    for (const int gain : {0, 300}) {
        const std::wstring base = Path((stem + std::to_wstring(gain)).c_str());
        const Take t = Record(g_exe, L"-o " + Quote(base) + L" -f wav " + options + L" " + gainOption + L" " + std::to_wstring(gain), tones);
        AudioProbe p(base + fileSuffix);
        pair.ok = pair.ok && t.started && p.Valid();
        (gain == 0 ? pair.at0 : pair.at300) = p.Valid() ? measure(p) : 0.0;
    }
    return pair;
}

void T7()
{
    Section("T7  system audio gain (--system-amplify 0 vs 300)");
    const GainPair g = RecordGains(L"t7_", L"-i --separate", L"--system-amplify", L"_system.wav", OneTone(), ToneDb);
    if (g.ok) {
        const double d = g.at300 - g.at0;
        Check("gain 300 is about 12 dB louder than gain 0 (x4)", d > 10.0 && d < 14.0, Fmt("tone peak %.1f dB -> %.1f dB, %+.1f dB", g.at0, g.at300, d));
    } else {
        Check("both gain files were written", false);
    }
}

void T8()
{
    Section("T8  microphone gain (-a 0 vs -a 300)");
    const GainPair g = RecordGains(L"t8_", L"", L"-a", L".wav", {}, [](const AudioProbe &p) { return p.MeanDb(0.0, p.Seconds()); });
    if (!g.ok) {
        Check("both microphone gain files were written", false);
    } else if (g.at0 <= -85.0) {
        Info(Fmt("the microphone at gain 0 is digital silence (%.1f dB): its gain cannot be measured here", g.at0));
    } else {
        const double d = g.at300 - g.at0;
        Check("gain 300 is about 12 dB louder than gain 0 (x4)", d > 8.0 && d < 16.0,
              Fmt("mean %.1f dB -> %.1f dB, %+.1f dB; the room is not identical between takes", g.at0, g.at300, d));
    }
}

void T9()
{
    Section("T9  device selection by id, name and index");
    const Device &mic = DefaultOf(g_mics);
    const Device &speaker = DefaultOf(g_speakers);
    const std::wstring base = Path(L"t9a");
    const Take t = Record(g_exe, L"-o " + Quote(base) + L" -f wav -m " + Quote(Widen(mic.id)) + L" -i -d " + Quote(Widen(speaker.name)) + L" --separate", OneTone());
    Check("microphone by id, playback device by name: --stop ok", t.stop.exitCode == 0, Fmt("code %d", t.stop.exitCode));
    if (auto p = CheckAudio("system track", base + L"_system.wav", t.elapsed, "wav"))
        Check("the tone was captured from the named device", ToneDb(*p) > -40.0, Fmt("tone window peak %.1f dB", ToneDb(*p)));

    int others = 0;
    for (const Device &d : g_speakers) {
        if (d.isDefault || others == 2)
            continue;
        ++others;
        const std::string tag = Fmt("playback device [%d]", d.index);
        const std::wstring dbase = Path((L"t9_dev" + std::to_wstring(d.index)).c_str());
        const Take r = Record(g_exe, L"-o " + Quote(dbase) + L" -f wav -m " + std::to_wstring(mic.index) + L" -i -d " + std::to_wstring(d.index) + L" --separate", OneTone());
        Check(tag + ": --stop ok", r.stop.exitCode == 0, Fmt("code %d", r.stop.exitCode));
        AudioProbe pm(dbase + L"_mic.wav"), ps(dbase + L"_system.wav");
        Check(tag + ": both tracks written, at least 30 s", pm.Valid() && ps.Valid() && pm.Seconds() >= 30.0 && ps.Seconds() >= 30.0);
        if (pm.Valid() && ps.Valid()) {
            CheckSameLength(tag, pm, ps);
            const double peak = ps.PeakDb(0.0, ps.Seconds());
            Check(tag + ": carries no audio (the tone went to the default device)", peak < -60.0, Fmt("peak %.1f dB", peak));
        }
    }
    if (others == 0)
        Info("only one playback device: the silent-device checks are skipped");
}

void T10()
{
    Section("T10 bad devices and a stray --stop");
    const std::wstring base = Path(L"t10");
    CliResult r = RunCli(g_exe, L"--defaults -o " + Quote(base) + L" -f wav -m 99");
    Check("an unknown microphone is rejected with a message", r.exitCode == 1 && r.output.find("no microphone matches") != std::string::npos,
          Fmt("code %d: %s", r.exitCode, r.output.c_str()));
    r = RunCli(g_exe, L"--defaults -o " + Quote(base) + L" -f wav -i -d 99");
    Check("an unknown playback device is rejected with a message", r.exitCode == 1 && r.output.find("99") != std::string::npos,
          Fmt("code %d: %s", r.exitCode, r.output.c_str()));
    r = RunCli(g_exe, L"--stop");
    Check("--stop with nothing recording says so", r.exitCode == 1 && r.output.find("No active recording") != std::string::npos,
          Fmt("code %d: %s", r.exitCode, r.output.c_str()));
    Check("no file was left behind", GetFileAttributesW((base + L".wav").c_str()) == INVALID_FILE_ATTRIBUTES);
}

void T11()
{
    Section("T11 closing the window while recording");
    const std::wstring base = Path(L"t11");
    const Take t = Record(g_exe, L"-o " + Quote(base) + L" -f mp3 -i --separate", OneTone(), true);
    Check("the window closed by itself with exit code 0", t.exitCode == 0, Fmt("exit %d", t.exitCode));
    CheckAudio("mic file", base + L"_mic.mp3", t.elapsed, "mp3", 2.0);
    CheckAudio("system file", base + L"_system.mp3", t.elapsed, "mp3", 2.0);
}

void T12()
{
    Section("T12 the other architecture's build, mic + system");
    if (GetFileAttributesW(g_otherExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Info("the other build is not there: skipped");
        return;
    }
    const std::wstring base = Path(L"t12");
    const Take t = Record(g_otherExe, L"-o " + Quote(base) + L" -f wav -i --separate", OneTone());
    Check("--stop reports success", t.stop.exitCode == 0, Fmt("code %d", t.stop.exitCode));
    auto mic = CheckAudio("mic track", base + L"_mic.wav", t.elapsed, "wav");
    auto sys = CheckAudio("system track", base + L"_system.wav", t.elapsed, "wav");
    if (sys && g_loopback)
        Check("the system tone was captured", ToneDb(*sys) > -40.0, Fmt("tone window peak %.1f dB", ToneDb(*sys)));
    if (mic && sys)
        CheckSameLength("other build", *mic, *sys);
}

void T13()
{
    Section("T13 stability over one recording (memory, handles, threads, CPU)");
    const std::wstring base = Path(L"t13");
    std::vector<Tone> tones;
    if (g_loopback)
        tones.push_back({1.0, &g_tone12});
    /* A minute at least: the program takes up handles and memory for the first half minute (Media
     * Foundation, COM, Direct2D load what they need) and must then hold steady. */
    const Take t = Record(g_exe, L"-o " + Quote(base) + L" -f mp3 -i --separate", tones, false, true, 60.0);
    if (t.usage.size() >= 4) {
        const AppProcess::Usage &first = t.usage[1], &settled = t.usage[t.usage.size() / 2], &z = t.usage.back();
        const double seconds = t.usageAt.back() - t.usageAt[t.usage.size() / 2];
        for (std::size_t i = 0; i < t.usage.size(); ++i)
            Info(Fmt("t=%5.1f s  private %.1f MB, %lu handles, %lu threads, CPU %.1f s", t.usageAt[i], t.usage[i].privateMb,
                     t.usage[i].handles, t.usage[i].threads, t.usage[i].cpuSeconds));
        const std::string half = Fmt("second half, %.0f s", seconds);
        Check("private memory is flat in the second half", z.privateMb - settled.privateMb < 3.0,
              Fmt("%+.1f MB, %s", z.privateMb - settled.privateMb, half.c_str()));
        Check("the handle count is flat in the second half", z.handles <= settled.handles + 3,
              Fmt("%lu -> %lu, %s", settled.handles, z.handles, half.c_str()));
        Check("the thread count is flat in the second half", z.threads <= settled.threads + 2,
              Fmt("%lu -> %lu, %s", settled.threads, z.threads, half.c_str()));
        const double cpu = 100.0 * (z.cpuSeconds - first.cpuSeconds) / (t.usageAt.back() - t.usageAt[1]);
        Check("CPU use is modest while recording", cpu < 15.0, Fmt("%.1f%% of one core", cpu));
    } else {
        Check("resource samples were taken", false);
    }
    auto mic = CheckAudio("mic file", base + L"_mic.mp3", t.elapsed, "mp3");
    auto sys = CheckAudio("system file", base + L"_system.mp3", t.elapsed, "mp3");
    if (mic && sys)
        CheckSameLength("long recording", *mic, *sys);
}

void T14()
{
    Section("T14 background windows kept the focus");
    if (g_focus.Samples() == 0) {
        Info("no recording ran, so nothing was observed");
        return;
    }
    if (g_options.foreground)
        Info("--foreground: the windows open normally, so the focus is not checked");
    else
        Check("the foreground window never belonged to the application during the run", g_focus.Held() == 0,
              g_focus.Summary() + (g_focus.Held() ? "; clicking a window yourself counts" : ""));
    if (!g_strangers.empty())
        Info(Fmt("%d other copy(ies) of the program ran during the run; they share the stop signal, so if a check above failed "
                 "unexpectedly (a recording that ended early), run the tests again",
                 static_cast<int>(g_strangers.size())));
}

/* ---- T15: the live transcript of what the PC plays ------------------------- */

/* What one run of the engine, listening to the system audio while a voice speaks, came out with. */
struct Heard {
    bool         live = false;                          /* the helper became ready */
    double       readySeconds = 0.0;
    double       firstTextAfter = -1.0;                 /* seconds after the voice began; < 0: no text at all */
    double       voiceSeconds = 0.0;                    /* how long the voice spoke */
    float        maxLag = 0.0f;
    bool         failed = false;
    bool         cutShort = false;                      /* the recording ended before the test stopped it */
    std::wstring finals, problem, failure;
};

double SecondsSince(std::chrono::steady_clock::time_point t) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count(); }

/* The default playback device says `text` (Windows' own speech synthesis), quietly; false if it cannot. */
bool StartSpeaking(ISpVoice **voice, const wchar_t *text)
{
    if (FAILED(CoCreateInstance(__uuidof(SpVoice), nullptr, CLSCTX_ALL, __uuidof(ISpVoice), reinterpret_cast<void **>(voice))))
        return false;
    (*voice)->SetVolume(60);
    return SUCCEEDED((*voice)->Speak(text, static_cast<DWORD>(SPF_ASYNC | SPF_IS_NOT_XML), nullptr));
}

/* Records the system audio with the real engine and a live transcript, with a voice speaking meanwhile. */
bool ListenToAVoice(bool gpu, const wchar_t *sentence, Heard *heard)
{
    audiorgui::WasapiEngine engine;
    audiorgui::RecordingSettings s;
    s.outputDirectory = g_dir->Dir();
    s.fileName = gpu ? L"voice-gpu" : L"voice-cpu";
    s.format = audiorgui::Format::Wav;
    s.includeMicrophone = false;
    s.includeSystemAudio = s.transcribeSystem = true;
    s.transcribeLanguage = audiorgui::SpeechLanguage::English;
    s.transcribeGpu = gpu;
    std::wstring error;
    if (!engine.Start(s, audiorgui::JoinPath(s.outputDirectory, audiorgui::EnforceExtension(s.fileName, s.format)), &error)) {
        heard->problem = L"the recording did not start: " + error;
        return false;
    }

    constexpr audiorgui::StreamId kSystem = audiorgui::StreamId::System;
    audiorgui::PeakBin peaks[256];
    audiorgui::TranscriptEvent events[8];
    auto drain = [&](double sinceVoice) {
        while (engine.TakePeaks(kSystem, peaks, 256) > 0) {}      /* the window's job; the queue is bounded, but be tidy */
        for (std::size_t n; (n = engine.TakeTranscript(kSystem, events, 8)) > 0;) {
            for (std::size_t i = 0; i < n; ++i) {
                if (heard->firstTextAfter < 0.0 && events[i].text[0] != L'\0')
                    heard->firstTextAfter = sinceVoice;
                if (events[i].isFinal)
                    heard->finals += std::wstring(events[i].text) + L" ";
            }
        }
        const audiorgui::TranscriptStatus status = engine.Transcription(kSystem);
        heard->maxLag = status.lagSeconds > heard->maxLag ? status.lagSeconds : heard->maxLag;
        if (status.state == audiorgui::TranscriptState::Failed) {
            heard->failed = true;
            heard->problem = engine.TranscriptionProblem(kSystem);
        }
    };

    const auto began = std::chrono::steady_clock::now();
    while (SecondsSince(began) < 120.0 && !heard->failed) {       /* the helper checks its files and loads the model first */
        drain(-1.0);
        if (engine.Transcription(kSystem).state == audiorgui::TranscriptState::Live) {
            heard->live = true;
            break;
        }
        Sleep(50);
    }
    heard->readySeconds = SecondsSince(began);

    ISpVoice *voice = nullptr;
    if (heard->live && StartSpeaking(&voice, sentence)) {
        const auto voiceBegan = std::chrono::steady_clock::now();
        SPVOICESTATUS state = {};
        while (SecondsSince(voiceBegan) < 40.0 && !heard->failed) {
            voice->GetStatus(&state, nullptr);
            if (state.dwRunningState & SPRS_DONE) {
                if (heard->voiceSeconds == 0.0)
                    heard->voiceSeconds = SecondsSince(voiceBegan);
                if (SecondsSince(voiceBegan) > heard->voiceSeconds + 4.0)      /* a few seconds for the last words */
                    break;
            }
            drain(SecondsSince(voiceBegan));
            Sleep(50);
        }
    }
    if (voice)
        voice->Release();

    heard->cutShort = !engine.IsRecording();                      /* e.g. another program's --stop: they share one signal */
    engine.Stop();                                                /* the helper says its last words */
    drain(heard->voiceSeconds);
    heard->failure = engine.Failure();
    return heard->live;
}

void T15()
{
    Section("T15 live transcript of the system audio");
    audiorgui::WasapiEngine probe;                              /* only looks at files: no device is opened */
    if (!probe.CanTranscribe()) {
        Info("skipped: the transcription helper (build.bat asr) and a model are not in place (see the README)");
        return;
    }
    const bool gpuToo = probe.CanTranscribeOnGpu();
    static const wchar_t kSentence[] = L"Testing, one, two, three. The quick brown fox jumps over the lazy dog. Please write this down.";
    static const wchar_t *const kWords[] = {L"quick", L"brown", L"fox", L"lazy", L"dog"};

    for (const bool gpu : {false, true}) {
        if (gpu && !gpuToo)
            continue;
        const char *label = gpu ? "graphics card" : "processor";
        const std::size_t helpersBefore = FindProcesses(L"audior_asr.exe").size();
        Heard heard;
        ListenToAVoice(gpu, kSentence, &heard);

        Check(Fmt("%s: the recording starts and the helper becomes ready", label), heard.live,
              heard.live ? Fmt("ready after %.1f s", heard.readySeconds) : Narrow(heard.problem));
        if (!heard.live)
            continue;
        Check(Fmt("%s: the recording is not cut short by anything else", label), !heard.cutShort,
              heard.cutShort ? "another program sent the stop signal, which every copy of the program shares: run the test again" : "");
        Check(Fmt("%s: the transcript does not fail", label), !heard.failed && heard.failure.empty(),
              Narrow(heard.problem + heard.failure));
        Check(Fmt("%s: text appears while the voice is still speaking", label),
              heard.firstTextAfter >= 0.0 && heard.firstTextAfter < heard.voiceSeconds,
              heard.firstTextAfter < 0.0 ? "no text at all" : Fmt("first text %.1f s in, the voice spoke for %.1f s", heard.firstTextAfter, heard.voiceSeconds));
        std::wstring lowered = heard.finals;
        for (wchar_t &c : lowered)
            c = static_cast<wchar_t>(std::towlower(c));
        int found = 0;
        for (const wchar_t *word : kWords)
            found += lowered.find(word) != std::wstring::npos ? 1 : 0;
        Check(Fmt("%s: the words that were said are in the text", label), found >= 4,
              Fmt("%d of %d key words; heard: \"%s\"", found, static_cast<int>(sizeof(kWords) / sizeof(kWords[0])), Narrow(heard.finals).c_str()));
        Check(Fmt("%s: the text is never far behind", label), heard.maxLag < 3.0f, Fmt("at most %.1f s behind", heard.maxLag));

        std::size_t helpersAfter = FindProcesses(L"audior_asr.exe").size();      /* it should be gone a moment after Stop() */
        for (int i = 0; i < 40 && helpersAfter > helpersBefore; ++i) {
            Sleep(100);
            helpersAfter = FindProcesses(L"audior_asr.exe").size();
        }
        Check(Fmt("%s: no helper is left running", label), helpersAfter <= helpersBefore, Fmt("%d before, %d after", static_cast<int>(helpersBefore), static_cast<int>(helpersAfter)));
    }
    if (!gpuToo)
        Info("the helper has no graphics card backend (build.bat vulkan), so only the processor was tried");
}

/* ---- T16: a busy PC makes the transcript change to a smaller model ---------- */

/* Every core kept busy at normal priority: the helper runs below normal, so it gets what is left. */
class CpuLoad {
public:
    void Start()
    {
        const unsigned cores = std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 4;
        for (unsigned i = 0; i < cores; ++i) {
            threads_.emplace_back([this] {
                std::uint32_t x = 1;
                while (!stop_.load(std::memory_order_relaxed)) {
                    for (int k = 0; k < 100000; ++k)
                        x = x * 1664525u + 1013904223u;
                    sink_.store(x, std::memory_order_relaxed);          /* so that the loop is not optimised away */
                }
            });
        }
    }
    void Stop()
    {
        stop_ = true;
        for (std::thread &t : threads_)
            t.join();
        threads_.clear();
    }
    bool Running() const { return !threads_.empty(); }
    ~CpuLoad() { Stop(); }

private:
    std::atomic<bool>          stop_{false};
    std::atomic<std::uint32_t> sink_{0};
    std::vector<std::thread>   threads_;
};

/* Reads the samples of a 16 kHz mono 16-bit WAV file. */
bool ReadSamples(const std::wstring &path, std::vector<std::int16_t> *samples)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size = {};
    std::vector<std::uint8_t> bytes;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 44 && size.QuadPart < (16 << 20)) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) || got != bytes.size())
            bytes.clear();
    }
    CloseHandle(file);
    for (std::size_t at = 12; bytes.size() > 44 && at + 8 <= bytes.size();) {
        std::uint32_t length;
        std::memcpy(&length, bytes.data() + at + 4, 4);
        if (std::memcmp(bytes.data() + at, "data", 4) == 0) {
            const std::size_t count = std::min<std::size_t>(length, bytes.size() - at - 8) / 2;
            samples->resize(count);
            std::memcpy(samples->data(), bytes.data() + at + 8, count * 2);
            return count > 0;
        }
        at += 8 + length + (length & 1);
    }
    return false;
}

/* Windows' own speech voice says `text` into a file, silently, as 16 kHz mono 16-bit samples. */
bool SpeechSamples(const wchar_t *text, std::vector<std::int16_t> *samples)
{
    static const GUID kWaveFormatEx = {0xC31ADBAE, 0x527F, 0x4ff5, {0xA2, 0x30, 0xF6, 0x2B, 0xB6, 0x1F, 0xF7, 0x0C}};
    const std::wstring path = Path(L"speech.wav");
    ISpVoice *voice = nullptr;
    ISpStream *stream = nullptr;
    bool ok = SUCCEEDED(CoCreateInstance(__uuidof(SpVoice), nullptr, CLSCTX_ALL, __uuidof(ISpVoice), reinterpret_cast<void **>(&voice))) &&
              SUCCEEDED(CoCreateInstance(__uuidof(SpStream), nullptr, CLSCTX_ALL, __uuidof(ISpStream), reinterpret_cast<void **>(&stream)));
    if (ok) {
        WAVEFORMATEX format = {WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0};
        ok = SUCCEEDED(stream->BindToFile(path.c_str(), SPFM_CREATE_ALWAYS, &kWaveFormatEx, &format, 0)) &&
             SUCCEEDED(voice->SetOutput(stream, TRUE)) &&
             SUCCEEDED(voice->Speak(text, static_cast<DWORD>(SPF_IS_NOT_XML), nullptr));    /* returns when it is all in the file */
        stream->Close();
    }
    if (stream)
        stream->Release();
    if (voice)
        voice->Release();
    return ok && ReadSamples(path, samples);
}

/* About a minute of speech for T16 and T17, with words to look for early on and in the last sentences. */
const wchar_t kLongSpeech[] =
    L"Good morning. Today the weather is mild, the river is calm, and the mountain road is open. "
    L"We meet at the station at nine, and then we walk to the old market together. "
    L"The first stall sells bread and honey, the second one sells wool and tea. "
    L"My neighbour says that the best coffee in town is made behind the church, "
    L"although nobody agrees about it, and everybody has a different favourite place. "
    L"After lunch we climb the hill, take a few photographs, and watch the boats on the lake. "
    L"In the evening the streets fill with music, children laugh, and the lamps are lit one by one. "
    L"Please remember the schedule for tomorrow: breakfast at seven, a short meeting at eight, "
    L"and the long drive to the coast after that. "
    L"And finally, at the very end, a purple banana sat quietly beside the open window.";
const wchar_t *const kEarlyWords[] = {L"river", L"mountain", L"station", L"market", L"honey"};
const wchar_t *const kLateWords[] = {L"purple", L"banana", L"window", L"coast", L"schedule"};

/* How many words (runs of non-blanks) there are in `text`. */
std::size_t WordsIn(const std::wstring &text)
{
    std::size_t words = 0;
    bool inWord = false;
    for (const wchar_t c : text) {
        const bool blank = std::iswspace(c) != 0;
        words += !blank && !inWord ? 1 : 0;
        inWord = !blank;
    }
    return words;
}

int CountWords(const std::wstring &text, const wchar_t *const *words, std::size_t count)
{
    std::wstring lowered = text;
    for (wchar_t &c : lowered)
        c = static_cast<wchar_t>(std::towlower(c));
    int found = 0;
    for (std::size_t i = 0; i < count; ++i)
        found += lowered.find(words[i]) != std::wstring::npos ? 1 : 0;
    return found;
}

void T16()
{
    Section("T16 a busy PC: the transcript changes to a lighter model");
    const audiorgui::AsrModel *big = audiorgui::FindAsrModel(L"ggml-large-v3.bin");
    const audiorgui::AsrModel *lesser = audiorgui::FindAsrModel(L"ggml-large-v3-turbo-q5_0.bin");
    audiorgui::AsrSetup setup, fallback;
    const std::wstring programs = audiorgui::ProgramDirectory(), models = audiorgui::ModelsDirectory();
    if (!big || !lesser || !audiorgui::FindAsr(programs, models, &setup, audiorgui::kAsrModels, audiorgui::kAsrModelCount, big->file) ||
        !audiorgui::FindAsr(programs, models, &fallback, audiorgui::kAsrModels, audiorgui::kAsrModelCount, lesser->file)) {
        Info("skipped: it needs the helper (build.bat asr) and both Whisper large-v3 and its turbo model");
        return;
    }

    std::vector<std::int16_t> speech;
    if (!SpeechSamples(kLongSpeech, &speech)) {
        Check("a voice could be synthesized into a file", false);
        return;
    }
    Info(Fmt("%.0f s of speech, large model on the processor, language fixed to English; every core is kept busy once it is ready",
             static_cast<double>(speech.size()) / 16000.0));

    audiorgui::TranscriptionHost::Options options;
    options.model = setup.modelPath;
    options.vadModel = setup.vadPath;
    options.language = audiorgui::SpeechLanguage::English;
    options.gpu = false;
    options.fallbacks.push_back({fallback.modelPath, audiorgui::MakeProcessFactory(fallback)});
    audiorgui::TranscriptionHost host(audiorgui::MakeProcessFactory(setup), options);
    constexpr audiorgui::StreamId kStream = audiorgui::StreamId::Microphone;
    const std::size_t helpersBefore = FindProcesses(L"audior_asr.exe").size();
    host.Start(static_cast<std::uint8_t>(1u << audiorgui::StreamIndex(kStream)));

    bool sawCatchingUp = false, smaller = false, recovered = false;
    double catchingAt = -1.0, smallerAt = -1.0, maxLag = 0.0, nextReport = 0.0;
    std::wstring finals;
    audiorgui::TranscriptEvent events[8];
    CpuLoad load;
    auto observe = [&](double now) {
        const audiorgui::TranscriptStatus status = host.Status(kStream);
        maxLag = status.lagSeconds > maxLag ? status.lagSeconds : maxLag;
        if (status.state == audiorgui::TranscriptState::CatchingUp && !sawCatchingUp) {
            sawCatchingUp = true;
            catchingAt = now;
        }
        if (status.smallerModel && !smaller) {
            smaller = true;
            smallerAt = now;
        }
        if (smaller && load.Running() == false && status.state == audiorgui::TranscriptState::Live)
            recovered = true;
        for (std::size_t n; (n = host.TakeEvents(kStream, events, 8)) > 0;) {
            for (std::size_t i = 0; i < n; ++i) {
                if (events[i].isFinal)
                    finals += std::wstring(events[i].text) + L" ";
            }
        }
        if (now >= nextReport) {
            nextReport = now + 10.0;
            Info(Fmt("t=%5.1f s  state %d, %.1f s behind%s%s", now, static_cast<int>(status.state), status.lagSeconds,
                     status.smallerModel ? ", smaller model" : "", load.Running() ? ", PC busy" : ""));
        }
    };

    const auto began = std::chrono::steady_clock::now();
    auto now = [&] { return SecondsSince(began); };
    while (now() < 120.0 && host.Status(kStream).state != audiorgui::TranscriptState::Live)       /* the model is read without the load */
        Sleep(100);
    const bool ready = host.Status(kStream).state == audiorgui::TranscriptState::Live;
    Check("the large model becomes ready", ready, Fmt("after %.1f s", now()));
    if (ready) {
        load.Start();
        const double started = now();
        std::size_t at = 0;
        while (at < speech.size()) {                                    /* the speech, at the pace of speech */
            const std::size_t chunk = std::min<std::size_t>(1600, speech.size() - at);
            host.Push(kStream, speech.data() + at, chunk);
            at += chunk;
            observe(now());
            if (smaller && load.Running() && now() > smallerAt + 5.0)    /* the busy time is over a few seconds after the change */
                load.Stop();
            const double due = started + static_cast<double>(at) / 16000.0;
            while (now() < due)
                Sleep(10);
        }
        if (load.Running() && !smaller)
            Info("the speech is over and the model never changed");
        const double waitUntil = now() + 60.0;
        while (now() < waitUntil && !(smaller && recovered && host.Status(kStream).lagSeconds < 1.0)) {
            if (smaller && load.Running() && now() > smallerAt + 5.0)
                load.Stop();
            observe(now());
            Sleep(100);
        }
        load.Stop();
    }
    host.Stop();
    observe(now());

    Check("the text fell behind while the PC was busy (it said it was catching up)", sawCatchingUp,
          sawCatchingUp ? Fmt("at %.1f s, at most %.1f s behind", catchingAt, maxLag) : Fmt("at most %.1f s behind", maxLag));
    Check("it changed to the smaller model", smaller, smaller ? Fmt("at %.1f s", smallerAt) : "never");
    const int found = CountWords(finals, kLateWords, sizeof(kLateWords) / sizeof(kLateWords[0]));
    Check("the last sentences, said after the change, are in the text", found >= 3,
          Fmt("%d of %d key words; the end of the text: \"%s\"", found, static_cast<int>(sizeof(kLateWords) / sizeof(kLateWords[0])),
              Narrow(finals.size() > 200 ? finals.substr(finals.size() - 200) : finals).c_str()));
    Check("it is live again once the PC is quiet", recovered);
    std::size_t helpersAfter = FindProcesses(L"audior_asr.exe").size();
    for (int i = 0; i < 40 && helpersAfter > helpersBefore; ++i) {
        Sleep(100);
        helpersAfter = FindProcesses(L"audior_asr.exe").size();
    }
    Check("no helper is left running", helpersAfter <= helpersBefore, Fmt("%d before, %d after", static_cast<int>(helpersBefore), static_cast<int>(helpersAfter)));
}

/* ---- T17: the large model on the graphics card, at the pace of speech --------- */

struct LargeRun {
    bool         ready = false;
    double       readySeconds = 0.0;
    double       firstTextAfter = -1.0;                 /* seconds after the speech began; < 0: no text */
    bool         anyFinal = false;
    double       lastFinalAfterEnd = 0.0;               /* seconds after the speech ended that the last phrase was settled (< 0: before) */
    double       maxLag = 0.0, meanLag = 0.0;
    int          early = 0, late = 0;                   /* key words found, of the early and of the last sentences */
    int          words = 0;                             /* all the words of the settled text */
    std::wstring finals;
};

/* The speech goes to a helper with the large model, at the pace of speech and with no smaller model to fall back on. */
LargeRun RunLarge(const audiorgui::AsrSetup &setup, bool gpu, const std::vector<std::int16_t> &speech)
{
    LargeRun run;
    audiorgui::TranscriptionHost::Options options;
    options.model = setup.modelPath;
    options.vadModel = setup.vadPath;
    options.language = audiorgui::SpeechLanguage::English;
    options.gpu = gpu;
    options.engine = setup.model->engine;
    audiorgui::TranscriptionHost host(audiorgui::MakeProcessFactory(setup), options);
    constexpr audiorgui::StreamId kStream = audiorgui::StreamId::Microphone;
    host.Start(static_cast<std::uint8_t>(1u << audiorgui::StreamIndex(kStream)));

    const auto began = std::chrono::steady_clock::now();
    auto now = [&] { return SecondsSince(began); };
    while (now() < 180.0 && host.Status(kStream).state != audiorgui::TranscriptState::Live)       /* checks the files, loads the model */
        Sleep(50);
    run.ready = host.Status(kStream).state == audiorgui::TranscriptState::Live;
    run.readySeconds = now();
    if (!run.ready) {
        host.Stop();
        return run;
    }

    const double speechSeconds = static_cast<double>(speech.size()) / 16000.0;
    const double started = now();
    double lagSum = 0.0;
    int lagCount = 0;
    audiorgui::TranscriptEvent events[8];
    auto observe = [&] {
        const audiorgui::TranscriptStatus status = host.Status(kStream);
        run.maxLag = status.lagSeconds > run.maxLag ? status.lagSeconds : run.maxLag;
        lagSum += status.lagSeconds;
        ++lagCount;
        for (std::size_t n; (n = host.TakeEvents(kStream, events, 8)) > 0;) {
            for (std::size_t i = 0; i < n; ++i) {
                if (run.firstTextAfter < 0.0 && events[i].text[0] != L'\0')
                    run.firstTextAfter = now() - started;
                if (events[i].isFinal) {
                    run.finals += std::wstring(events[i].text) + L" ";
                    run.lastFinalAfterEnd = now() - started - speechSeconds;
                    run.anyFinal = true;
                }
            }
        }
    };
    std::size_t at = 0;
    while (at < speech.size()) {                                        /* at the pace of speech */
        const std::size_t chunk = std::min<std::size_t>(1600, speech.size() - at);
        host.Push(kStream, speech.data() + at, chunk);
        at += chunk;
        observe();
        const double due = started + static_cast<double>(at) / 16000.0;
        while (now() < due)
            Sleep(10);
    }
    const double settleUntil = now() + 40.0;                            /* what is left, until it is quiet */
    double lastFinalSeen = run.lastFinalAfterEnd;
    double quietSince = now();
    while (now() < settleUntil && now() - quietSince < 4.0) {
        observe();
        if (run.lastFinalAfterEnd != lastFinalSeen) {
            lastFinalSeen = run.lastFinalAfterEnd;
            quietSince = now();
        }
        Sleep(50);
    }
    host.Stop();
    observe();
    run.meanLag = lagCount ? lagSum / lagCount : 0.0;
    run.early = CountWords(run.finals, kEarlyWords, sizeof(kEarlyWords) / sizeof(kEarlyWords[0]));
    run.late = CountWords(run.finals, kLateWords, sizeof(kLateWords) / sizeof(kLateWords[0]));
    run.words = static_cast<int>(WordsIn(run.finals));
    return run;
}

void T17()
{
    Section("T17 the large model on the graphics card, at the pace of speech");
    const audiorgui::AsrModel *big = audiorgui::FindAsrModel(L"ggml-large-v3-turbo-q5_0.bin");
    audiorgui::AsrSetup setup;
    if (!big || !audiorgui::FindAsr(audiorgui::ProgramDirectory(), audiorgui::ModelsDirectory(), &setup, audiorgui::kAsrModels,
                                    audiorgui::kAsrModelCount, big->file) || !setup.gpu) {
        Info("skipped: it needs the helper with the Vulkan backend (build.bat vulkan) and the large (turbo) model");
        return;
    }
    std::vector<std::int16_t> speech;
    if (!SpeechSamples(kLongSpeech, &speech)) {
        Check("a voice could be synthesized into a file", false);
        return;
    }
    const double speechSeconds = static_cast<double>(speech.size()) / 16000.0;
    Info(Fmt("%.0f s of speech, given at the pace of speech to the large model, on the graphics card and then on the processor", speechSeconds));

    for (const bool gpu : {true, false}) {
        const char *label = gpu ? "graphics card" : "processor";
        const std::size_t helpersBefore = FindProcesses(L"audior_asr.exe").size();
        const LargeRun r = RunLarge(setup, gpu, speech);
        Info(Fmt("%s: ready after %.1f s; first text %.1f s into the speech; lag %.1f s on average, %.1f s at most; the last phrase settled "
                 "%.1f s after the speech ended; key words %d of 5 early, %d of 5 late; %d words of the text for %d spoken",
                 label, r.readySeconds, r.firstTextAfter, r.meanLag, r.maxLag, r.lastFinalAfterEnd, r.early, r.late, r.words,
                 static_cast<int>(WordsIn(kLongSpeech))));
        if (!gpu) {
            Check("processor: the helper becomes ready and gives text", r.ready && r.firstTextAfter >= 0.0);       /* the numbers above are for comparison */
        } else {
            Check("graphics card: the helper becomes ready", r.ready, Fmt("after %.1f s", r.readySeconds));
            Check("graphics card: text begins soon after the speech does", r.firstTextAfter >= 0.0 && r.firstTextAfter < 10.0,
                  r.firstTextAfter < 0.0 ? "no text at all" : Fmt("%.1f s (the first use of the card takes a few seconds longer)", r.firstTextAfter));
            Check("graphics card: the text is never far behind", r.ready && r.maxLag < 3.0, Fmt("at most %.1f s, on average %.1f s", r.maxLag, r.meanLag));
            Check("graphics card: the last phrase is settled soon after the speech ends", r.anyFinal && r.lastFinalAfterEnd < 5.0,
                  Fmt("%.1f s after", r.lastFinalAfterEnd));
            Check("graphics card: nothing is said twice", r.words <= static_cast<int>(WordsIn(kLongSpeech)) * 115 / 100,
                  Fmt("%d words for %d spoken", r.words, static_cast<int>(WordsIn(kLongSpeech))));
            Check("graphics card: the words that were said are in the text", r.early >= 4 && r.late >= 4,
                  Fmt("%d of 5 early and %d of 5 late; the end of the text: \"%s\"", r.early, r.late,
                      Narrow(r.finals.size() > 160 ? r.finals.substr(r.finals.size() - 160) : r.finals).c_str()));
        }
        std::size_t helpersAfter = FindProcesses(L"audior_asr.exe").size();
        for (int i = 0; i < 40 && helpersAfter > helpersBefore; ++i) {
            Sleep(100);
            helpersAfter = FindProcesses(L"audior_asr.exe").size();
        }
        Check(Fmt("%s: no helper is left running", label), helpersAfter <= helpersBefore,
              Fmt("%d before, %d after", static_cast<int>(helpersBefore), static_cast<int>(helpersAfter)));
    }
}

/* ---- T18: Parakeet at the pace of speech --------------------------------------- */

void T18()
{
    Section("T18 Parakeet at the pace of speech");
    const audiorgui::AsrModel *parakeet = audiorgui::FindAsrModel(L"ggml-parakeet-tdt-0.6b-v3-q8_0.bin");
    audiorgui::AsrSetup setup;
    if (!parakeet || !audiorgui::FindAsr(audiorgui::ProgramDirectory(), audiorgui::ModelsDirectory(), &setup, audiorgui::kAsrModels,
                                         audiorgui::kAsrModelCount, parakeet->file)) {
        Info("skipped: it needs the helper (build.bat asr) and the Parakeet model in the models folder (it is not offered for download)");
        return;
    }
    std::vector<std::int16_t> speech;
    if (!SpeechSamples(kLongSpeech, &speech)) {
        Check("a voice could be synthesized into a file", false);
        return;
    }
    Info(Fmt("%.0f s of speech, given at the pace of speech to Parakeet%s", static_cast<double>(speech.size()) / 16000.0,
             setup.gpu ? ", on the processor and then on the graphics card" : ", on the processor (the helper has no graphics card backend)"));

    for (const bool gpu : {false, true}) {
        if (gpu && !setup.gpu)
            continue;
        const char *label = gpu ? "graphics card" : "processor";
        const std::size_t helpersBefore = FindProcesses(L"audior_asr.exe").size();
        const LargeRun r = RunLarge(setup, gpu, speech);
        Info(Fmt("%s: ready after %.1f s; first text %.1f s into the speech; lag %.1f s on average, %.1f s at most; the last phrase settled "
                 "%.1f s after the speech ended; key words %d of 5 early, %d of 5 late; %d words of the text for %d spoken",
                 label, r.readySeconds, r.firstTextAfter, r.meanLag, r.maxLag, r.lastFinalAfterEnd, r.early, r.late, r.words,
                 static_cast<int>(WordsIn(kLongSpeech))));
        Check(Fmt("%s: the helper becomes ready", label), r.ready, Fmt("after %.1f s", r.readySeconds));
        Check(Fmt("%s: the text is never far behind", label), r.ready && r.maxLag < 3.0, Fmt("at most %.1f s, on average %.1f s", r.maxLag, r.meanLag));
        Check(Fmt("%s: the last phrase is settled soon after the speech ends", label), r.anyFinal && r.lastFinalAfterEnd < 5.0,
              Fmt("%.1f s after", r.lastFinalAfterEnd));
        Check(Fmt("%s: nothing is said twice", label), r.words <= static_cast<int>(WordsIn(kLongSpeech)) * 115 / 100,
              Fmt("%d words for %d spoken", r.words, static_cast<int>(WordsIn(kLongSpeech))));
        Check(Fmt("%s: the words that were said are in the text", label), r.early >= 4 && r.late >= 4,
              Fmt("%d of 5 early and %d of 5 late; the end of the text: \"%s\"", r.early, r.late,
                  Narrow(r.finals.size() > 160 ? r.finals.substr(r.finals.size() - 160) : r.finals).c_str()));
        std::size_t helpersAfter = FindProcesses(L"audior_asr.exe").size();
        for (int i = 0; i < 40 && helpersAfter > helpersBefore; ++i) {
            Sleep(100);
            helpersAfter = FindProcesses(L"audior_asr.exe").size();
        }
        Check(Fmt("%s: no helper is left running", label), helpersAfter <= helpersBefore,
              Fmt("%d before, %d after", static_cast<int>(helpersBefore), static_cast<int>(helpersAfter)));
    }
}

struct TestEntry {
    const char *id;
    void      (*run)();
    bool        needsLoopback;
};

const TestEntry kTests[] = {
    {"T1", T1, false}, {"T2", T2, false}, {"T3", T3, false},  {"T4", T4, false},   {"T5", T5, true},
    {"T6", T6, true},  {"T7", T7, true},  {"T8", T8, false},  {"T9", T9, true},    {"T10", T10, false},
    {"T11", T11, true}, {"T12", T12, false}, {"T13", T13, false}, {"T14", T14, false}, {"T15", T15, true}, {"T16", T16, false}, {"T17", T17, false}, {"T18", T18, false},
};

/* ---- start-up ------------------------------------------------------------- */

std::wstring DirectoryOfThisProgram()
{
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, n);
    return dir.substr(0, dir.rfind(L'\\'));
}

bool ParseArguments(int argc, wchar_t **argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--foreground") {
            g_options.foreground = true;
        } else if (a == L"--no-sound") {
            g_options.noSound = true;
        } else if (a == L"--keep") {
            g_options.keep = true;
        } else if (a == L"--min-seconds" && i + 1 < argc) {
            g_options.minSeconds = _wtof(argv[++i]);
        } else if (a == L"--only" && i + 1 < argc) {
            std::string list = Narrow(argv[++i]), item;
            for (char c : list + ",") {
                if (c == ',') {
                    if (!item.empty())
                        g_options.only.push_back(item);
                    item.clear();
                } else {
                    item += c;
                }
            }
        } else {
            std::printf("Usage: hardware_tests [--only T1,T6] [--min-seconds 32] [--foreground] [--no-sound] [--keep]\n");
            return false;
        }
    }
    if (g_options.minSeconds < 1.0) {
        std::printf("--min-seconds must be a number of at least 1.\n");
        return false;
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetConsoleOutputCP(CP_UTF8);
    if (!ParseArguments(argc, argv))
        return kSetupError;

    const std::wstring dir = DirectoryOfThisProgram();
    g_exe = dir + L"\\Audior-GUI.exe";
    const bool x64 = sizeof(void *) == 8;
    g_otherExe = dir + (x64 ? L"\\..\\x86\\Audior-GUI.exe" : L"\\..\\x64\\Audior-GUI.exe");
    if (GetFileAttributesW(g_exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::printf("%s was not found: build the application first.\n", Narrow(g_exe).c_str());
        return kSetupError;
    }

    for (const wchar_t *program : {kExeNames[0], kExeNames[1], L"hardware_tests.exe"}) {
        for (const DWORD id : FindProcesses(program)) {
            if (id != GetCurrentProcessId()) {
                std::printf("%s is already running (process %lu). All instances share one stop signal and the microphone, "
                            "so it would cut these recordings short: close it first.\n", Narrow(program).c_str(), id);
                return kSetupError;
            }
        }
    }

    const CliResult devices = RunCli(g_exe, L"--list-devices");
    ReadDevices(devices.output);
    std::printf("Devices: %d microphone(s), %d playback device(s); recordings of at least %.0f s each\n", static_cast<int>(g_mics.size()),
                static_cast<int>(g_speakers.size()), g_options.minSeconds);
    if (g_mics.empty() || g_speakers.empty()) {
        std::printf("A microphone and a playback device are needed.\n");
        return kSetupError;
    }

    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);

    g_dir = std::make_unique<TempDir>();
    g_loopback = !g_options.noSound;
    if (g_loopback) {
        g_tone2 = Path(L"tone2.wav");
        g_tone12 = Path(L"tone12.wav");
        if (!WriteTone(g_tone2, 2) || !WriteTone(g_tone12, 12)) {
            std::printf("The test tone could not be written.\n");
            return kSetupError;
        }
    }

    for (const TestEntry &t : kTests) {
        if (Wanted(t.id) && (g_loopback || !t.needsLoopback))
            t.run();
    }
    MFShutdown();
    if (SUCCEEDED(com))
        CoUninitialize();
    if (g_options.keep) {
        std::printf("\nRecordings kept in %s\n", Narrow(g_dir->Dir()).c_str());
        static_cast<void>(g_dir.release());             /* never destroyed: the folder stays */
    }
    g_dir.reset();

    int failed = 0;
    for (const Outcome &o : g_outcomes)
        failed += o.ok ? 0 : 1;
    std::printf("\nHARDWARE TESTS: %d checks, %d passed, %d failed\n", static_cast<int>(g_outcomes.size()),
                static_cast<int>(g_outcomes.size()) - failed, failed);
    for (const Outcome &o : g_outcomes) {
        if (!o.ok)
            std::printf("  FAIL [%s] %s  [%s]\n", o.section.c_str(), o.what.c_str(), o.detail.c_str());
    }
    return failed < 100 ? failed : 100;
}
