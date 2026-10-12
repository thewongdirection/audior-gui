/* Tests of the job that writes the clearer copies (readers and sinks in memory), and of the file readers. */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../src/engine/mp3_sink.h"
#include "../src/engine/pcm.h"
#include "../src/engine/pcm_reader.h"
#include "../src/engine/voice_enhance_job.h"
#include "../src/engine/wav_sink.h"
#include "temp_dir.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

using Samples = std::vector<std::int16_t>;

/* A tone with noise under it, `frames` long, the same in both channels; `toneDb` and `noiseDb` are RMS dBFS. */
Samples Voice(std::size_t frames, double toneDb, double noiseDb)
{
    const double tone = std::pow(10.0, toneDb / 20.0) * std::sqrt(2.0) * 32768.0;
    const double noise = std::pow(10.0, noiseDb / 20.0) * std::sqrt(3.0) * 32768.0;
    std::uint32_t state = 12345;
    Samples s(2 * frames);
    for (std::size_t i = 0; i < frames; ++i) {
        state = state * 1664525u + 1013904223u;
        const double random = static_cast<double>(static_cast<std::int32_t>(state)) / 2147483648.0;
        const double envelope = 0.6 + 0.4 * std::sin(6.2831853 * 3.0 * static_cast<double>(i) / 48000.0);
        s[2 * i] = s[2 * i + 1] = static_cast<std::int16_t>(
            tone * envelope * std::sin(6.2831853 * 440.0 * static_cast<double>(i) / 48000.0) + noise * random);
    }
    return s;
}

double RmsDb(const Samples &s)
{
    double sum = 0.0;
    for (std::int16_t v : s)
        sum += static_cast<double>(v) * v;
    return 10.0 * std::log10(sum / static_cast<double>(s.size()) / (32768.0 * 32768.0) + 1e-20);
}

/* ---- a reader that plays a block of samples, or noise for ever; a sink that keeps what it is given ---- */

class MemoryReader final : public IPcmReader {
public:
    MemoryReader(std::shared_ptr<const Samples> audio, bool endless) : audio_(std::move(audio)), endless_(endless) {}

    HRESULT Read(std::int16_t *out, std::size_t maxFrames, std::size_t *frames) override
    {
        if (endless_) {
            Sleep(1);                                       /* not a busy loop while a test waits */
            for (std::size_t i = 0; i < 2 * maxFrames; ++i)
                out[i] = static_cast<std::int16_t>((i * 7919) % 2000);
            *frames = maxFrames;
            return S_OK;
        }
        *frames = std::min(maxFrames, audio_->size() / 2 - at_);
        std::copy_n(audio_->begin() + static_cast<std::ptrdiff_t>(2 * at_), 2 * *frames, out);
        at_ += *frames;
        return S_OK;
    }
    std::uint64_t TotalFrames() const override { return endless_ ? 0 : audio_->size() / 2; }

private:
    std::shared_ptr<const Samples> audio_;
    bool                           endless_;
    std::size_t                    at_ = 0;
};

struct SinkState {
    Samples                    samples;
    std::atomic<std::uint64_t> bytes{0};
    std::atomic<bool>          closed{false};
    std::atomic<bool>          kept{false};
};

class MemorySink final : public IAudioSink {
public:
    MemorySink(std::shared_ptr<SinkState> state, std::uint64_t failAfterBytes, HRESULT writeHr)
        : state_(std::move(state)), failAfter_(failAfterBytes), writeHr_(writeHr) {}

    HRESULT Write(const void *pcm, std::size_t bytes) override
    {
        if (state_->bytes + bytes > failAfter_)
            return writeHr_;
        const std::int16_t *p = static_cast<const std::int16_t *>(pcm);
        state_->samples.insert(state_->samples.end(), p, p + bytes / sizeof(std::int16_t));
        state_->bytes += bytes;
        return S_OK;
    }
    HRESULT Close(bool keep) override
    {
        state_->kept = keep;
        state_->closed = true;
        return S_OK;
    }
    std::uint64_t BytesWritten() const override { return state_->bytes; }

private:
    std::shared_ptr<SinkState> state_;
    std::uint64_t              failAfter_;
    HRESULT                    writeHr_;
};

/* What the job is given to read and write, and a count of what it asked for.  Declare it before the job. */
struct Rig {
    std::shared_ptr<const Samples> audio = std::make_shared<const Samples>(Voice(96000, -40.0, -62.0));
    bool                           endless = false;         /* the second reading of a file never ends       */
    HRESULT                        openHr = S_OK;           /* the reader cannot be made, with this           */
    HRESULT                        sinkHr = S_OK;           /* the sink cannot be made, with this              */
    std::uint64_t                  failAfterBytes = ~0ull;  /* the sink fails after this much                  */
    std::atomic<int>               readers{0}, sinks{0};
    std::mutex                     mutex;
    std::vector<std::shared_ptr<SinkState>> made;

    VoiceEnhanceJob::ReaderFactory Readers()
    {
        return [this](const std::wstring &, HRESULT *hr) -> std::unique_ptr<IPcmReader> {
            ++readers;
            *hr = openHr;
            if (FAILED(openHr))
                return nullptr;
            return std::make_unique<MemoryReader>(audio, endless && readers > 1);     /* the first read ends, the second never */
        };
    }
    VoiceEnhanceJob::SinkFactory Sinks()
    {
        return [this](const std::wstring &, HRESULT *hr) -> std::unique_ptr<IAudioSink> {
            ++sinks;
            *hr = sinkHr;
            if (FAILED(sinkHr))
                return nullptr;
            auto state = std::make_shared<SinkState>();
            std::lock_guard<std::mutex> lock(mutex);
            made.push_back(state);
            return std::make_unique<MemorySink>(state, failAfterBytes, HRESULT_FROM_WIN32(ERROR_DISK_FULL));
        };
    }
    std::shared_ptr<SinkState> Sink(std::size_t i)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return i < made.size() ? made[i] : nullptr;
    }
};

std::vector<VoiceEnhanceJob::Task> Tasks(std::size_t n)
{
    std::vector<VoiceEnhanceJob::Task> tasks;
    for (std::size_t i = 0; i < n; ++i)
        tasks.push_back({L"in" + std::to_wstring(i) + L".wav", L"in" + std::to_wstring(i) + L" - enhanced.wav"});
    return tasks;
}

/* Waits for the job to stop working (it takes a moment of real time). */
EnhanceStatus Wait(const VoiceEnhanceJob &job)
{
    EnhanceStatus status = job.Status();
    for (int i = 0; i < 10000 && status.state == EnhanceState::Working; ++i) {
        Sleep(5);
        status = job.Status();
    }
    return status;
}

}  // namespace

TEST(EnhanceJob_WritesAClearerCopyOfTheSameLength)
{
    Rig rig;
    VoiceEnhanceJob job(rig.Readers(), rig.Sinks(), Tasks(1));
    const EnhanceStatus status = Wait(job);
    CHECK(status.state == EnhanceState::Done);
    CHECK_EQ(status.progress, 1.0f);
    CHECK_EQ(job.Done().size(), std::size_t(1));
    CHECK(job.Done()[0] == L"in0 - enhanced.wav");

    const std::shared_ptr<SinkState> copy = rig.Sink(0);
    CHECK(copy != nullptr);
    CHECK(copy->closed && copy->kept);
    CHECK_EQ(copy->samples.size(), rig.audio->size());          /* every frame, no more and no fewer */
    CHECK(RmsDb(copy->samples) > RmsDb(*rig.audio) + 10.0);     /* the quiet voice has been brought up */
}

TEST(EnhanceJob_ReadsAFileTwiceAndWritesItOnce)
{
    Rig rig;
    VoiceEnhanceJob job(rig.Readers(), rig.Sinks(), Tasks(2));
    CHECK(Wait(job).state == EnhanceState::Done);
    CHECK_EQ(rig.readers.load(), 4);
    CHECK_EQ(rig.sinks.load(), 2);
    CHECK_EQ(job.Done().size(), std::size_t(2));
}

TEST(EnhanceJob_ProgressNeverGoesBack)
{
    Rig rig;
    rig.audio = std::make_shared<const Samples>(Voice(192000, -40.0, -62.0));      /* four seconds: long enough to watch */
    VoiceEnhanceJob job(rig.Readers(), rig.Sinks(), Tasks(1));
    float last = 0.0f;
    EnhanceStatus status = job.Status();
    while (status.state == EnhanceState::Working) {
        CHECK(status.progress >= last && status.progress <= 1.0f);
        last = status.progress;
        Sleep(2);
        status = job.Status();
    }
    CHECK(status.state == EnhanceState::Done);
    CHECK(status.progress >= last);
}

TEST(EnhanceJob_CancellingRemovesTheUnfinishedCopy)
{
    Rig rig;
    rig.endless = true;
    VoiceEnhanceJob job(rig.Readers(), rig.Sinks(), Tasks(1));
    for (int i = 0; i < 4000 && !(rig.Sink(0) && rig.Sink(0)->bytes > 0); ++i)       /* until the copy has begun */
        Sleep(5);
    CHECK(rig.Sink(0) != nullptr);
    if (!rig.Sink(0))
        return;

    job.Cancel();
    CHECK(job.Status().state == EnhanceState::Cancelled);
    CHECK(job.Done().empty());
    CHECK(rig.Sink(0)->closed && !rig.Sink(0)->kept);
}

TEST(EnhanceJob_CancellingAFinishedJobChangesNothing)
{
    Rig rig;
    VoiceEnhanceJob job(rig.Readers(), rig.Sinks(), Tasks(1));
    CHECK(Wait(job).state == EnhanceState::Done);
    job.Cancel();
    CHECK(job.Status().state == EnhanceState::Done);
    CHECK_EQ(job.Done().size(), std::size_t(1));
}

TEST(EnhanceJob_AFileThatCannotBeReadFailsAndMakesNoCopy)
{
    Rig rig;
    rig.openHr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    VoiceEnhanceJob job(rig.Readers(), rig.Sinks(), Tasks(2));
    const EnhanceStatus status = Wait(job);
    CHECK(status.state == EnhanceState::Failed);
    CHECK(status.problem.find(L"could not be enhanced") != std::wstring::npos);
    CHECK_EQ(rig.sinks.load(), 0);
    CHECK_EQ(rig.readers.load(), 1);                            /* and the second file is not tried */
    CHECK(job.Done().empty());
}

TEST(EnhanceJob_ACopyThatCannotBeCreatedIsAFailure)
{
    Rig rig;
    rig.sinkHr = HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);         /* what a sink says about a file that is there */
    VoiceEnhanceJob job(rig.Readers(), rig.Sinks(), Tasks(1));
    const EnhanceStatus status = Wait(job);
    CHECK(status.state == EnhanceState::Failed);
    CHECK(status.problem.find(L"already exists") != std::wstring::npos);
    CHECK(job.Done().empty());
}

TEST(EnhanceJob_ACopyThatCannotBeWrittenIsRemoved)
{
    Rig rig;
    rig.failAfterBytes = 100000;                                /* the disk fills up */
    VoiceEnhanceJob job(rig.Readers(), rig.Sinks(), Tasks(1));
    const EnhanceStatus status = Wait(job);
    CHECK(status.state == EnhanceState::Failed);
    CHECK(status.problem.find(L"disk is full") != std::wstring::npos);
    CHECK(rig.Sink(0) != nullptr);
    if (!rig.Sink(0))
        return;
    CHECK(rig.Sink(0)->closed && !rig.Sink(0)->kept);
    CHECK(job.Done().empty());
}

TEST(EnhanceJob_AnEmptyRecordingGivesAnEmptyCopy)
{
    Rig rig;
    rig.audio = std::make_shared<const Samples>();
    VoiceEnhanceJob job(rig.Readers(), rig.Sinks(), Tasks(1));
    CHECK(Wait(job).state == EnhanceState::Done);
    CHECK(rig.Sink(0) != nullptr);
    if (rig.Sink(0))
        CHECK_EQ(rig.Sink(0)->samples.size(), std::size_t(0));
}

/* ---- the readers ---- */

namespace {

Samples ReadAll(IPcmReader &reader, std::size_t piece)
{
    Samples all, block(2 * piece);
    for (;;) {
        std::size_t frames = 0;
        if (FAILED(reader.Read(block.data(), piece, &frames)) || frames == 0)
            return all;
        all.insert(all.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(2 * frames));
    }
}

void WriteWav(const std::wstring &path, const Samples &samples)
{
    HRESULT hr = S_OK;
    auto sink = WavSink::Create(path, &hr);
    CHECK(sink != nullptr);
    if (!sink)
        return;
    CHECK(SUCCEEDED(sink->Write(samples.data(), samples.size() * sizeof(std::int16_t))));
    CHECK(SUCCEEDED(sink->Close(true)));
}

void WriteBytes(const std::wstring &path, const std::string &bytes)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
}

std::string Le32(std::uint32_t v) { return std::string(reinterpret_cast<const char *>(&v), 4); }
std::string Le16(std::uint16_t v) { return std::string(reinterpret_cast<const char *>(&v), 2); }

/* A WAV file's bytes: a "fmt " chunk for the given format, any `extra` chunks, then a "data" chunk that claims
 * `claimed` bytes and holds `data`. */
std::string WavBytes(std::uint16_t channels, std::uint32_t rate, std::uint16_t bits, const std::string &extra,
                     std::uint32_t claimed, const std::string &data)
{
    const std::string fmt = Le16(1) + Le16(channels) + Le32(rate) + Le32(rate * channels * bits / 8) +
                            Le16(static_cast<std::uint16_t>(channels * bits / 8)) + Le16(bits);
    return "RIFF" + Le32(36) + "WAVE" + "fmt " + Le32(16) + fmt + extra + "data" + Le32(claimed) + data;
}

}  // namespace

TEST(WavReader_ReadsBackWhatWavSinkWrote)
{
    TempDir dir;
    const Samples original = Voice(10001, -20.0, -50.0);
    WriteWav(dir.Path(L"a.wav"), original);

    for (std::size_t piece : {std::size_t(1), std::size_t(777), std::size_t(20000)}) {
        HRESULT hr = S_OK;
        auto reader = OpenPcmReader(dir.Path(L"a.wav"), &hr);
        CHECK(reader != nullptr);
        if (!reader)
            return;
        CHECK_EQ(reader->TotalFrames(), std::uint64_t(10001));
        CHECK(ReadAll(*reader, piece) == original);
    }
}

TEST(WavReader_StepsOverChunksItDoesNotKnow)
{
    TempDir dir;
    const std::string odd = "LIST" + Le32(3) + "abc" + std::string(1, '\0');           /* an odd length is padded */
    const Samples samples = {1, -1, 2, -2, 3, -3};
    WriteBytes(dir.Path(L"b.wav"), WavBytes(2, 48000, 16, odd, 12, std::string(reinterpret_cast<const char *>(samples.data()), 12)));
    HRESULT hr = S_OK;
    auto reader = OpenWavReader(dir.Path(L"b.wav"), &hr);
    CHECK(reader != nullptr);
    if (reader)
        CHECK(ReadAll(*reader, 100) == samples);
}

TEST(WavReader_AFileCutShortGivesWhatIsThere)
{
    TempDir dir;
    const std::string data(4 * 100, '\x10');                                            /* 100 frames of the 1000 it claims */
    WriteBytes(dir.Path(L"c.wav"), WavBytes(2, 48000, 16, "", 4000, data));
    HRESULT hr = S_OK;
    auto reader = OpenWavReader(dir.Path(L"c.wav"), &hr);
    CHECK(reader != nullptr);
    if (reader) {
        CHECK_EQ(reader->TotalFrames(), std::uint64_t(100));
        CHECK_EQ(ReadAll(*reader, 64).size(), std::size_t(200));
    }
}

TEST(WavReader_OnlyTheRecordersOwnFormatIsRead)
{
    TempDir dir;
    const std::string data(400, '\0');
    WriteBytes(dir.Path(L"mono.wav"), WavBytes(1, 48000, 16, "", 400, data));
    WriteBytes(dir.Path(L"slow.wav"), WavBytes(2, 44100, 16, "", 400, data));
    WriteBytes(dir.Path(L"bits.wav"), WavBytes(2, 48000, 8, "", 400, data));
    WriteBytes(dir.Path(L"nodata.wav"), WavBytes(2, 48000, 16, "", 0, "").substr(0, 36));
    WriteBytes(dir.Path(L"text.wav"), "this is not a wave file at all, but it is long enough to have a header");
    WriteBytes(dir.Path(L"empty.wav"), "");
    for (const wchar_t *name : {L"mono.wav", L"slow.wav", L"bits.wav", L"nodata.wav", L"text.wav", L"empty.wav"}) {
        HRESULT hr = S_OK;
        CHECK(OpenPcmReader(dir.Path(name), &hr) == nullptr);
        CHECK(FAILED(hr));
    }
}

TEST(WavReader_HostileChunkLengthsAreRefusedOrSteppedOver)
{
    TempDir dir;
    const std::string fmt = Le16(1) + Le16(2) + Le32(48000) + Le32(192000) + Le16(4) + Le16(16);
    const std::string head = "RIFF" + Le32(36) + "WAVE";
    const std::string data = "data" + Le32(8) + std::string(8, '\x01');
    std::string junkRun;
    for (int i = 0; i < 10000; ++i)
        junkRun += "JUNK" + Le32(0);

    const struct { const wchar_t *name; std::string bytes; bool opens; } cases[] = {
        {L"bigjunk.wav", head + "JUNK" + Le32(0xFFFFFFFFu) + "fmt ", false},                  /* a length that wraps when padded */
        {L"bigfmt.wav", head + "fmt " + Le32(0xFFFFFFFFu) + fmt, false},
        {L"longfmt.wav", head + "fmt " + Le32(41) + fmt + std::string(25, '\0') + data, false},
        {L"oddfmt.wav", head + "fmt " + Le32(17) + fmt + std::string(2, '\0') + data, true},   /* 17 bytes and a pad byte */
        {L"cutheader.wav", head + "fmt " + Le32(16) + fmt + "da", false},                    /* ends inside a chunk header */
        {L"datafirst.wav", head + data + "fmt " + Le32(16) + fmt, false},                     /* no format before the data */
        {L"junkrun.wav", head + junkRun + "fmt " + Le32(16) + fmt + data, true},              /* many empty chunks: no loop */
    };
    for (const auto &c : cases) {
        WriteBytes(dir.Path(c.name), c.bytes);
        HRESULT hr = S_OK;
        auto reader = OpenWavReader(dir.Path(c.name), &hr);
        CHECK_EQ(reader != nullptr, c.opens);
        CHECK_EQ(SUCCEEDED(hr), c.opens);
        if (reader && c.opens)
            CHECK_EQ(ReadAll(*reader, 16).size(), std::size_t(4));                            /* the two frames of the data chunk */
    }
}

TEST(PcmReader_ASpellingOfTheExtensionDoesNotMatterAndOthersAreRefused)
{
    TempDir dir;
    WriteWav(dir.Path(L"UPPER.WAV"), Voice(100, -20.0, -50.0));
    HRESULT hr = S_OK;
    CHECK(OpenPcmReader(dir.Path(L"UPPER.WAV"), &hr) != nullptr);

    WriteBytes(dir.Path(L"x.flac"), "fLaC");
    CHECK(OpenPcmReader(dir.Path(L"x.flac"), &hr) == nullptr);
    CHECK_EQ(hr, HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE));
    CHECK(OpenPcmReader(dir.Path(L"missing.wav"), &hr) == nullptr);
    CHECK_EQ(hr, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
}

TEST(Mp3Reader_DecodesWhatMp3SinkEncoded)
{
    if (!Mp3Sink::Supported())
        SKIP("Media Foundation is not installed");
    TempDir dir;
    const Samples tone = Voice(96000, -9.0, -90.0);                     /* two seconds, loud and clean */
    HRESULT hr = S_OK;
    {
        auto sink = Mp3Sink::Create(dir.Path(L"t.mp3"), 128, &hr);
        if (!sink)
            SKIP("no MP3 encoder here");
        CHECK(SUCCEEDED(sink->Write(tone.data(), tone.size() * sizeof(std::int16_t))));
        CHECK(SUCCEEDED(sink->Close(true)));
    }
    auto reader = OpenPcmReader(dir.Path(L"t.mp3"), &hr);     /* the encoder worked, so the decoder is there: no skipping */
    CHECK(reader != nullptr);
    if (!reader)
        return;
    const Samples decoded = ReadAll(*reader, 4096);

    /* The codec adds a little silence at the start and the end; the length is within 10% and the level close. */
    CHECK(decoded.size() > tone.size() * 9 / 10 && decoded.size() < tone.size() * 11 / 10);
    CHECK_NEAR(RmsDb(decoded), RmsDb(tone), 1.5);
    CHECK(reader->TotalFrames() > 80000 && reader->TotalFrames() < 110000);
}
