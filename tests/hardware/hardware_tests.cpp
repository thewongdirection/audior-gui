/*
 * hardware_tests.cpp - Records with the real audior_gui.exe on this PC's real audio hardware
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
 *     foreground window belonged to the application: clicking one yourself shows up there.
 *   * The recordings go to a scratch folder under %TEMP% that is removed at the end (--keep
 *     leaves it).
 *
 * T1 MP3 and T2 WAV from the microphone, T3 MP3 bit rates, T4 file-name collisions, T5 merged
 * mic + system audio, T6 separate tracks and the padding that keeps them in step, T7 system
 * gain, T8 microphone gain, T9 choosing devices by id, name and index, T10 bad devices and a
 * stray --stop, T11 closing the window while recording, T12 the other architecture's build,
 * T13 memory, handles, threads and CPU over a recording, T14 the focus.
 *
 * Not automatable (docs\FIRST-TEST.md): unplugging a device, a loud voice for the CLIP flag,
 * the communications-ducking opt-out, and how the window looks.
 */
#include <windows.h>

#include <mfapi.h>
#include <mmsystem.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../temp_dir.h"
#include "app_process.h"
#include "audio_probe.h"

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
int                  g_focusSamples = 0;
int                  g_focusStolen = 0;

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

/* Starts the program, plays the tones, stops it with --stop after the minimum recording time (or
 * `seconds`, if longer) or, with `closeInstead`, by closing its window, and closes the window. */
Take Record(const std::wstring &exe, const std::wstring &args, const std::vector<Tone> &tones = {}, bool closeInstead = false,
            bool sampleUsage = false, double seconds = 0.0)
{
    const double length = seconds > g_options.minSeconds ? seconds : g_options.minSeconds;
    Take take;
    AppProcess app;
    if (!app.Start(exe, args, !g_options.foreground) || !app.HasWindow())
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
        ++g_focusSamples;
        if (ForegroundProcessId() == app.Id())
            ++g_focusStolen;
        Sleep(100);
    }
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
    CliResult r = RunCli(g_exe, L"-o " + Quote(base) + L" -f wav -m 99");
    Check("an unknown microphone is rejected with a message", r.exitCode == 1 && r.output.find("no microphone matches") != std::string::npos,
          Fmt("code %d: %s", r.exitCode, r.output.c_str()));
    r = RunCli(g_exe, L"-o " + Quote(base) + L" -f wav -i -d 99");
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
    if (g_options.foreground) {
        Info("--foreground: the windows open normally, so there is nothing to check");
        return;
    }
    if (g_focusSamples == 0) {
        Info("no recording ran, so the focus was not observed");
        return;
    }
    Check("the foreground window never belonged to the application during the run", g_focusStolen == 0,
          Fmt("%d of %d samples; clicking a window yourself counts", g_focusStolen, g_focusSamples));
}

struct TestEntry {
    const char *id;
    void      (*run)();
    bool        needsLoopback;
};

const TestEntry kTests[] = {
    {"T1", T1, false}, {"T2", T2, false}, {"T3", T3, false},  {"T4", T4, false},   {"T5", T5, true},
    {"T6", T6, true},  {"T7", T7, true},  {"T8", T8, false},  {"T9", T9, true},    {"T10", T10, false},
    {"T11", T11, true}, {"T12", T12, false}, {"T13", T13, false}, {"T14", T14, false},
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
    g_exe = dir + L"\\audior_gui.exe";
    const bool x64 = sizeof(void *) == 8;
    g_otherExe = dir + (x64 ? L"\\..\\x86\\audior_gui.exe" : L"\\..\\x64\\audior_gui.exe");
    if (GetFileAttributesW(g_exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::printf("%s was not found: build the application first.\n", Narrow(g_exe).c_str());
        return kSetupError;
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
