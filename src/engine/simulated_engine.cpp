#include "simulated_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <utility>

#include "../core/asr_languages.h"
#include "../core/asr_models.h"

namespace audiorgui {

namespace {

constexpr double kPi = 3.14159265358979323846;

/* Rates for sizing the pretend output, as audior's writers produce. */
constexpr double kWavBytesPerSecond = 48000.0 * 2 * 2;      /* 48 kHz, 16-bit, stereo */

/* ---- the transcript script ------------------------------------------- */

constexpr double kStartupSeconds = 0.8;             /* the "model" loads: status Starting       */
constexpr float  kLagSeconds     = 0.4f;
constexpr float  kBehindLagSeconds = 4.8f;          /* when FallBehindAfter() has been told to */
constexpr double kWordSeconds    = 0.26;            /* one word is spoken, and shown, per 0.26 s */
constexpr double kFinalDelay     = 0.45;            /* from the last word to the final event     */
constexpr double kScriptSeconds  = 42.0;            /* the conversation then starts over         */

/* One utterance: when it starts, what is said, and one word the engine first
 * hears wrongly (fixWord, shown as wrongText for two words before it is
 * corrected; -1: none). */
struct Utterance {
    double         at;
    const wchar_t *text;
    int            fixWord;
    const wchar_t *wrongText;
};

constexpr Utterance kMicScript[] = {
    {0.8,  L"Okay, let's get started. Thanks everyone for joining on short notice.", -1, nullptr},
    {5.0,  L"The agenda today is the Q4 launch timeline and the open risks.", -1, nullptr},
    {9.0,  L"Maya, can you walk us through where the build stands?", -1, nullptr},
    {22.5, L"Got it. So the blocker is the installer signing, not the code.", -1, nullptr},
    {32.0, L"Let's put a hard date on that by Friday.", -1, nullptr},
};

constexpr Utterance kSystemScript[] = {
    {13.0, L"Sure. The core features are done and QA signed off on Tuesday.", -1, nullptr},
    {17.5, L"What's still open is code signing for the installer. We're waiting on the certificate from IT.",
     5, L"sign in"},
    {26.5, L"If that lands this week we can still hit the launch date, otherwise it slips about five days.",
     -1, nullptr},
    {35.0, L"I'll chase them again today.", -1, nullptr},
};

/* The same meeting in Chinese, which has no spaces: it is "heard" two characters at a time. */
constexpr Utterance kMicScriptChinese[] = {
    {0.8,  L"好的，我们开始吧。感谢大家这么快就赶过来。", -1, nullptr},
    {5.0,  L"今天的议程是第四季度的发布时间表和遗留的风险。", -1, nullptr},
    {9.0,  L"小玛，你能给我们讲讲构建现在进展到哪里了吗？", -1, nullptr},
    {22.5, L"明白了，卡住我们的是安装程序的签名，不是代码。", -1, nullptr},
    {32.0, L"我们周五之前把日期定下来。", -1, nullptr},
};

constexpr Utterance kSystemScriptChinese[] = {
    {13.0, L"可以。核心功能已经完成，质量检查周二已经通过了。", -1, nullptr},
    {17.5, L"还没完成的是安装程序的代码签名，我们正在等 IT 部门的证书。", -1, nullptr},
    {26.5, L"如果证书这周能到，我们还赶得上发布日期，否则要推迟大约五天。", -1, nullptr},
    {35.0, L"我今天再去催一催。", -1, nullptr},
};

struct Script {
    const Utterance *items;
    std::size_t      count;
};

Script ScriptOf(StreamId stream, bool chinese)
{
    const bool mic = stream == StreamId::Microphone;
    if (chinese)
        return mic ? Script{kMicScriptChinese, sizeof(kMicScriptChinese) / sizeof(kMicScriptChinese[0])}
                   : Script{kSystemScriptChinese, sizeof(kSystemScriptChinese) / sizeof(kSystemScriptChinese[0])};
    return mic ? Script{kMicScript, sizeof(kMicScript) / sizeof(kMicScript[0])}
               : Script{kSystemScript, sizeof(kSystemScript) / sizeof(kSystemScript[0])};
}

/* The end of the word at `word`: the next space or, in Chinese, two characters on. */
const wchar_t *WordEnd(const wchar_t *word)
{
    const wchar_t *end = word;
    if (*end >= 0x2E80) {
        for (int i = 0; i < 2 && *end >= 0x2E80; ++i)
            ++end;
    } else {
        while (*end && *end != L' ')
            ++end;
    }
    return end;
}

int WordCount(const wchar_t *text)
{
    int words = 0;
    for (const wchar_t *word = text; *word; ++words) {
        word = WordEnd(word);
        word += *word == L' ';
    }
    return words;
}

/* Where event number `index` of a stream falls: each utterance is one partial
 * per word (step 0 shows one word) and then its final (step == words). */
struct Position {
    std::uint64_t cycle;
    std::size_t   utterance;
    int           step;
    int           words;
};

Position Locate(const Script &script, std::uint64_t index)
{
    std::uint64_t perCycle = 0;
    for (std::size_t i = 0; i < script.count; ++i)
        perCycle += static_cast<std::uint64_t>(WordCount(script.items[i].text)) + 1;

    Position p{index / perCycle, 0, 0, 0};
    std::uint64_t rest = index % perCycle;
    for (std::size_t i = 0; i < script.count; ++i) {
        const int words = WordCount(script.items[i].text);
        if (rest < static_cast<std::uint64_t>(words) + 1) {
            p.utterance = i;
            p.step = static_cast<int>(rest);
            p.words = words;
            break;
        }
        rest -= static_cast<std::uint64_t>(words) + 1;
    }
    return p;
}

double EventTime(const Script &script, const Position &p)
{
    const double start = script.items[p.utterance].at + static_cast<double>(p.cycle) * kScriptSeconds;
    return p.step < p.words ? start + p.step * kWordSeconds : start + p.words * kWordSeconds + kFinalDelay;
}

/* The text of the first `shown` words, with the one misheard word as it was heard. */
void PartialText(const Utterance &u, int shown, TranscriptEvent *e)
{
    wchar_t text[kTranscriptChars];
    std::size_t length = 0;
    const wchar_t *word = u.text;
    for (int i = 0; i < shown && *word; ++i) {
        const wchar_t *end = WordEnd(word);
        const bool misheard = i == u.fixWord && shown > i && shown <= i + 2;
        const wchar_t *from = misheard ? u.wrongText : word;
        const std::size_t n = misheard ? std::wcslen(u.wrongText) : static_cast<std::size_t>(end - word);
        if (length + n + 2 >= kTranscriptChars)
            break;
        if (length && word[-1] == L' ')                 /* the space there was before this word, if any */
            text[length++] = L' ';
        std::wmemcpy(text + length, from, n);
        length += n;
        word = end + (*end == L' ');
    }
    e->SetText(text, length);
}

}  // namespace

SimulatedEngine::SimulatedEngine(Clock nowSeconds, std::uint64_t seed)
    : now_(std::move(nowSeconds)), seed_(seed)
{
}

void SimulatedEngine::FailAfter(double seconds, std::wstring message)
{
    failAfter_ = seconds;
    failMessage_ = std::move(message);
}

void SimulatedEngine::FailTranscriptAfter(double seconds, std::wstring message)
{
    transcriptFailAfter_ = seconds;
    transcriptFailMessage_ = std::move(message);
}

void SimulatedEngine::FallBehindAfter(double seconds)
{
    behindAfter_ = seconds;
}

std::vector<DeviceInfo> SimulatedEngine::ListDevices(DeviceKind kind)
{
    if (kind == DeviceKind::Microphone)
        return {{L"sim-mic-1", L"Demo Microphone Array", true},
                {L"sim-mic-2", L"Demo USB Microphone", false},
                {L"sim-mic-3", L"Demo Bluetooth Headset Microphone", false, true}};
    return {{L"sim-out-1", L"Demo Speakers", true},
            {L"sim-out-2", L"Demo Headphones", false}};
}

std::vector<TranscriptModel> SimulatedEngine::TranscriptModels() const
{
    std::vector<TranscriptModel> models;
    if (!modelsPresent_)
        return models;
    for (const AsrModel &m : kAsrModels)
        models.push_back(ModelChoice(m));
    return models;
}

bool SimulatedEngine::Start(const RecordingSettings &settings, const std::wstring &outputPath,
                            std::wstring *error)
{
    if (IsRecording()) {
        if (error)
            *error = L"A recording is already running.";
        return false;
    }
    settings_ = settings;
    files_ = PlannedOutputFiles(settings, outputPath);
    transcriptFiles_ = PlannedTranscriptFiles(settings, outputPath);
    failure_.clear();
    produced_[0] = produced_[1] = 0;
    startTime_ = endTime_ = now_();
    failAt_ = failAfter_;                   /* a failure applies to one recording only */
    failAfter_ = -1.0;
    transcriptNext_[0] = transcriptNext_[1] = 0;
    transcriptFailAt_ = transcriptFailAfter_;
    transcriptFailAfter_ = -1.0;
    behindAt_ = behindAfter_;
    behindAfter_ = -1.0;
    recording_ = true;
    return true;
}

/* A scheduled failure takes effect the first time it is looked at after its
 * moment; the recording is cut at that moment, not at the time of looking. */
bool SimulatedEngine::IsRecording()
{
    if (recording_ && failAt_ >= 0.0 && now_() - startTime_ >= failAt_) {
        recording_ = false;
        endTime_ = startTime_ + failAt_;
        failure_ = failMessage_;
    }
    return recording_;
}

void SimulatedEngine::Stop()
{
    if (!IsRecording())
        return;
    recording_ = false;
    endTime_ = now_();
}

double SimulatedEngine::Elapsed()
{
    if (IsRecording())
        return std::max(0.0, now_() - startTime_);
    return endTime_ - startTime_;
}

RecordingStats SimulatedEngine::Stats()
{
    RecordingStats stats;
    stats.seconds = Elapsed();
    const double perFile = settings_.format == Format::Wav
                               ? kWavBytesPerSecond
                               : settings_.mp3BitrateKbps * 125.0;     /* kbit/s -> bytes/s */
    stats.bytesWritten = static_cast<std::uint64_t>(stats.seconds * perFile * static_cast<double>(files_.size()));
    return stats;
}

std::size_t SimulatedEngine::TakePeaks(StreamId stream, PeakBin *out, std::size_t capacity)
{
    const int s = StreamIndex(stream);
    if (stream == StreamId::Microphone ? !settings_.includeMicrophone : !settings_.includeSystemAudio)
        return 0;

    const std::uint64_t due = static_cast<std::uint64_t>(Elapsed() * kPeakBinsPerSecond);
    if (due <= produced_[s])
        return 0;
    const std::size_t n = static_cast<std::size_t>(
        std::min<std::uint64_t>(capacity, due - produced_[s]));
    for (std::size_t i = 0; i < n; ++i)
        out[i] = Synthesize(stream, produced_[s] + i);
    produced_[s] += n;
    return n;
}

/* The recording time the transcript has reached: it stops where a failure of
 * the transcription cut it, while the recording runs on. */
double SimulatedEngine::TranscriptElapsed()
{
    const double elapsed = Elapsed();
    return transcriptFailAt_ >= 0.0 ? std::min(elapsed, transcriptFailAt_) : elapsed;
}

TranscriptStatus SimulatedEngine::Transcription(StreamId stream)
{
    TranscriptStatus status;
    if (!Transcribes(settings_, stream))
        return status;
    if (transcriptFailAt_ >= 0.0 && Elapsed() >= transcriptFailAt_) {
        status.state = TranscriptState::Failed;
    } else if (IsRecording()) {
        const bool loaded = Elapsed() >= kStartupSeconds;
        const bool behind = behindAt_ >= 0.0 && Elapsed() >= behindAt_;
        status.state = !loaded ? TranscriptState::Starting : behind ? TranscriptState::CatchingUp : TranscriptState::Live;
        status.lagSeconds = !loaded ? 0.0f : behind ? kBehindLagSeconds : kLagSeconds;
        status.smallerModel = behind;
    }
    return status;
}

std::size_t SimulatedEngine::TakeTranscript(StreamId stream, TranscriptEvent *out, std::size_t capacity)
{
    if (!Transcribes(settings_, stream))
        return 0;

    const Script script = ScriptOf(stream, std::strcmp(InfoOf(settings_.transcribeLanguage).code, "zh") == 0);
    const double elapsed = TranscriptElapsed();
    std::uint64_t &next = transcriptNext_[StreamIndex(stream)];
    std::size_t n = 0;
    for (; n < capacity; ++n, ++next) {
        const Position p = Locate(script, next);
        if (EventTime(script, p) > elapsed)
            break;

        const Utterance &u = script.items[p.utterance];
        TranscriptEvent &e = out[n];
        e.utterance = static_cast<std::uint32_t>(p.cycle * script.count + p.utterance);
        e.isFinal = p.step == p.words;
        e.startSeconds = u.at + static_cast<double>(p.cycle) * kScriptSeconds;
        if (e.isFinal)
            e.SetText(u.text, std::wcslen(u.text));
        else
            PartialText(u, p.step + 1, &e);
    }
    return n;
}

/* A repeatable pseudo-random number in [0, 1) for (a, b): splitmix64. */
double SimulatedEngine::Hash01(std::uint64_t a, std::uint64_t b) const
{
    std::uint64_t x = seed_ + a * 0x9E3779B97F4A7C15ull + b * 0xD1B54A32D192ED03ull;
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<double>(x >> 11) / 9007199254740992.0;
}

PeakBin SimulatedEngine::Synthesize(StreamId stream, std::uint64_t bin) const
{
    const double t = static_cast<double>(bin) / kPeakBinsPerSecond;
    double amp;

    if (stream == StreamId::Microphone) {
        /* Phrases of 2.4 s: most are "spoken" (a syllable rhythm under a
         * fade-in and fade-out), some are pauses with a little room noise. */
        const double position = t / 2.4;
        const double segment = std::floor(position);
        const double frac = position - segment;
        const std::uint64_t seg = static_cast<std::uint64_t>(segment);
        const bool speaking = Hash01(1, seg) > 0.30;
        const double edge = std::min(1.0, 8.0 * std::min(frac, 1.0 - frac));
        const double rate = 3.0 + 2.0 * Hash01(2, seg);
        const double s = std::sin(kPi * rate * t);
        const double syllable = 0.12 + 0.88 * s * s;
        const double jitter = Hash01(3, bin);
        amp = speaking ? 0.30 * edge * syllable * (0.7 + 0.3 * jitter) : 0.004 * jitter;
        amp *= 1.0 + settings_.micAmplifyPercent / 100.0;               /* gain, before clipping */
    } else {
        /* A steady beat (two a second) over a quieter bed. */
        const double beat = std::exp(-5.0 * std::fmod(t * 2.0, 1.0));
        amp = 0.42 * (0.30 + 0.70 * beat) * (0.85 + 0.15 * Hash01(4, bin));
        amp *= 1.0 + settings_.systemAmplifyPercent / 100.0;
    }

    PeakBin peak;
    peak.high = static_cast<float>(std::min(1.0, amp * (0.75 + 0.25 * Hash01(5, bin))));
    peak.low  = -static_cast<float>(std::min(1.0, amp * (0.75 + 0.25 * Hash01(6, bin))));
    return peak;
}

}  // namespace audiorgui
