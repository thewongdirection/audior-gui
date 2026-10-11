/*
 * Tests of the real transcription helper (audior_asr.exe).
 *
 * They need what a plain build does not make, the helper itself (`build.bat asr`
 * puts it next to run_tests.exe), and for the speech tests a model and a
 * recording, neither of which is in the repository.  Without them a test is
 * skipped and says so.  To run everything:
 *
 *     set AUDIOR_MODELS=<a folder holding a Whisper model, ggml-large-v3-turbo-q5_0.bin say>
 *     set AUDIOR_ASR_WAV=<a 16 kHz mono 16-bit WAV of speech>
 *     set AUDIOR_ASR_EXPECT=country            (a word the recording says; optional)
 *     bin\x64\run_tests.exe RealHelper_
 */
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "../src/core/asr_protocol.h"
#include "../src/engine/asr_helper.h"
#include "../src/engine/process_transport.h"
#include "../src/engine/wasapi_engine.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

using Clock = std::chrono::steady_clock;

std::wstring Env(const wchar_t *name)
{
    wchar_t value[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(name, value, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::wstring(value, n) : std::wstring();
}

std::wstring HelperPath() { return ProgramDirectory() + L"\\" + kHelperFile; }

bool HelperExists() { return GetFileAttributesW(HelperPath().c_str()) != INVALID_FILE_ATTRIBUTES; }

bool ReadWav(const std::wstring &path, std::vector<std::int16_t> *samples)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size = {};
    std::vector<std::uint8_t> data;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 44 && size.QuadPart < (64 << 20)) {
        data.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &got, nullptr) || got != data.size())
            data.clear();
    }
    CloseHandle(file);
    if (data.size() < 44 || std::memcmp(data.data(), "RIFF", 4) != 0)
        return false;
    for (std::size_t at = 12; at + 8 <= data.size();) {                 /* the "data" chunk */
        std::uint32_t length;
        std::memcpy(&length, data.data() + at + 4, 4);
        if (std::memcmp(data.data() + at, "data", 4) == 0) {
            const std::size_t bytes = std::min<std::size_t>(length, data.size() - at - 8);
            samples->resize(bytes / 2);
            std::memcpy(samples->data(), data.data() + at + 8, samples->size() * 2);
            return true;
        }
        at += 8 + length + (length & 1);
    }
    return false;
}

bool Send(ProcessTransport &helper, const asr::Message &m)
{
    std::vector<std::uint8_t> frame(asr::kMaxFrame);
    const std::size_t n = asr::Encode(m, frame.data(), frame.size());
    return n > 0 && helper.Write(frame.data(), n);
}

/* Reads until a whole message arrives; false if the helper hung up first. */
bool Receive(ProcessTransport &helper, asr::MessageReader &reader, asr::Message *m)
{
    std::uint8_t bytes[1024];
    for (;;) {
        const asr::MessageReader::Result r = reader.Next(m);
        if (r == asr::MessageReader::Result::Message)
            return true;
        if (r == asr::MessageReader::Result::Invalid)
            return false;
        const std::size_t n = helper.Read(bytes, sizeof(bytes));
        if (n == 0)
            return false;
        for (std::size_t used = 0; used < n;)
            used += reader.Append(bytes + used, n - used);
    }
}

}  // namespace

TEST(RealHelper_SaysWhyWhenTheModelIsMissing)
{
    if (!HelperExists())
        SKIP("audior_asr.exe is not next to run_tests.exe (build.bat asr)");
    std::wstring problem;
    std::unique_ptr<ProcessTransport> helper =
        ProcessTransport::Launch(HelperPath(), L"", ProcessTransport::Limits(), &problem);
    CHECK(helper != nullptr);
    if (!helper)
        return;

    asr::Message hello;
    hello.type = asr::Type::Hello;
    hello.streams = 3;
    hello.text = L"C:\\no\\such\\model.bin";
    CHECK(Send(*helper, hello));
    asr::MessageReader reader;
    asr::Message reply;
    CHECK(Receive(*helper, reader, &reply));
    CHECK(reply.type == asr::Type::Problem);
    CHECK(reply.all);
    CHECK(!reply.text.empty());
    CHECK(!Receive(*helper, reader, &reply));           /* and it ends */
}

TEST(RealHelper_LeavesWhenTheRecorderSendsNonsense)
{
    if (!HelperExists())
        SKIP("audior_asr.exe is not next to run_tests.exe (build.bat asr)");
    for (int variant = 0; variant < 3; ++variant) {
        std::wstring problem;
        std::unique_ptr<ProcessTransport> helper =
            ProcessTransport::Launch(HelperPath(), L"", ProcessTransport::Limits(), &problem);
        CHECK(helper != nullptr);
        if (!helper)
            return;
        const std::uint8_t garbage[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x77, 1, 2, 3};
        asr::Message m;
        if (variant == 0) {                             /* audio before any Hello */
            m.type = asr::Type::Audio;
            m.sampleCount = 160;
            CHECK(Send(*helper, m));
        } else if (variant == 1) {                      /* bytes that are no frame */
            CHECK(helper->Write(garbage, sizeof(garbage)));
        } else {                                        /* a message that only the helper may send */
            m.type = asr::Type::Ready;
            m.text = L"x";
            CHECK(Send(*helper, m));
        }
        std::uint8_t bytes[16];
        CHECK_EQ(helper->Read(bytes, sizeof(bytes)), std::size_t(0));       /* it hangs up */
    }
}

TEST(RealHelper_TheRecorderOffersTranscriptionOnlyWhenBothArePresent)
{
    AsrSetup setup;
    const bool both = FindAsr(ProgramDirectory(), ModelsDirectory(), &setup);
    WasapiEngine engine;                                /* only looks at files: no device is opened */
    CHECK_EQ(engine.CanTranscribe(), both);
    if (!both && !HelperExists())
        SKIP("audior_asr.exe is not next to run_tests.exe (build.bat asr): only the 'off' half was checked");
}

TEST(RealHelper_ThePackAndTheModelVerify)
{
    AsrSetup setup;
    if (!FindAsr(ProgramDirectory(), ModelsDirectory(), &setup))
        SKIP("the helper (build.bat asr) and a model (AUDIOR_MODELS or %LOCALAPPDATA%\\audior-gui\\models) are needed");
    CHECK_EQ(VerifyAsr(setup, true), std::wstring());   /* the manifest build.bat wrote, and the model's SHA-256 */
}

TEST(RealHelper_TranscribesARecordingThroughTheRecordersOwnPath)
{
    AsrSetup setup;
    if (!FindAsr(ProgramDirectory(), ModelsDirectory(), &setup))
        SKIP("the helper (build.bat asr) and a model (AUDIOR_MODELS or %LOCALAPPDATA%\\audior-gui\\models) are needed");
    std::vector<std::int16_t> speech;
    if (Env(L"AUDIOR_ASR_WAV").empty() || !ReadWav(Env(L"AUDIOR_ASR_WAV"), &speech))
        SKIP("AUDIOR_ASR_WAV must name a 16 kHz mono 16-bit WAV of speech");

    TranscriptionHost::Options options;
    options.model = setup.modelPath;
    options.engine = setup.model->engine;               /* as the recorder does: the model found may be a Parakeet one */
    TranscriptionHost host(MakeProcessFactory(setup), options);
    host.Start(1);

    /* At the pace of speech, as the recorder would give it. */
    constexpr std::size_t kChunk = 1600;
    std::vector<TranscriptEvent> events;
    TranscriptEvent buffer[8];
    auto collect = [&] {
        for (std::size_t n; (n = host.TakeEvents(StreamId::Microphone, buffer, 8)) > 0;)
            events.insert(events.end(), buffer, buffer + n);
    };
    bool wasLive = false;
    for (std::size_t at = 0; at < speech.size(); at += kChunk) {
        host.Push(StreamId::Microphone, speech.data() + at, std::min(kChunk, speech.size() - at));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wasLive = wasLive || host.Status(StreamId::Microphone).state == TranscriptState::Live;
        collect();
    }
    host.Stop();
    collect();

    CHECK(wasLive);
    CHECK(host.Status(StreamId::Microphone).state == TranscriptState::Off);      /* finished, not failed */
    CHECK(!events.empty());
    std::wstring finals;
    bool sawPartial = false;
    for (const TranscriptEvent &e : events) {
        sawPartial = sawPartial || !e.isFinal;
        if (e.isFinal)
            finals += (finals.empty() ? L"" : L" ") + std::wstring(e.text);
    }
    CHECK(sawPartial);                                  /* the text grew while the speech went on */
    CHECK(!finals.empty());
    const std::wstring expect = Env(L"AUDIOR_ASR_EXPECT");
    if (!expect.empty())
        CHECK(finals.find(expect) != std::wstring::npos);
}
