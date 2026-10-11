/* Tests of ProcessTransport with a real child process: run_tests.exe playing the helper (fake_asr_helper.h). */
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../src/core/asr_protocol.h"
#include "../src/engine/process_transport.h"
#include "fake_asr_helper.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

using Clock = std::chrono::steady_clock;

std::wstring ThisExe()
{
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::wstring(path, n);
}

std::unique_ptr<ProcessTransport> Start(const char *mode, std::wstring *problem)
{
    const std::string narrow = mode;
    const std::wstring arguments = std::wstring(L"") + L"--fake-asr-helper " + std::wstring(narrow.begin(), narrow.end());
    return ProcessTransport::Launch(ThisExe(), arguments, ProcessTransport::Limits(), problem);
}

template <typename Pred>
bool WaitUntil(Pred done, int timeoutMs = 10000)
{
    const Clock::time_point end = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (Clock::now() < end) {
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return done();
}

/* Kills the helper if a test waits for it too long, so a failure is a failure and not a hang. */
class Watchdog {
public:
    explicit Watchdog(ProcessTransport &transport, int seconds = 20)
        : thread_([this, &transport, seconds] {
              for (int i = 0; i < seconds * 10 && !done_; ++i)
                  std::this_thread::sleep_for(std::chrono::milliseconds(100));
              if (!done_)
                  transport.Close();
          })
    {
    }
    ~Watchdog()
    {
        done_ = true;
        thread_.join();
    }

private:
    std::atomic<bool> done_{false};
    std::thread       thread_;
};

bool Send(ProcessTransport &transport, const asr::Message &message)
{
    std::vector<std::uint8_t> frame(asr::kMaxFrame);
    const std::size_t n = asr::Encode(message, frame.data(), frame.size());
    return n > 0 && transport.Write(frame.data(), n);
}

/* Reads from the helper until one whole message has arrived; false if it hung up first. */
bool Receive(ProcessTransport &transport, asr::MessageReader &reader, asr::Message *message)
{
    std::uint8_t bytes[1024];
    for (;;) {
        const asr::MessageReader::Result r = reader.Next(message);
        if (r == asr::MessageReader::Result::Message)
            return true;
        if (r == asr::MessageReader::Result::Invalid)
            return false;
        const std::size_t n = transport.Read(bytes, sizeof(bytes));
        if (n == 0)
            return false;
        for (std::size_t used = 0; used < n;)
            used += reader.Append(bytes + used, n - used);
    }
}

asr::Message Hello()
{
    asr::Message m;
    m.type = asr::Type::Hello;
    m.streams = 1;
    m.text = L"model";
    return m;
}

asr::Message AudioOf(std::size_t samples)
{
    asr::Message m;
    m.type = asr::Type::Audio;
    m.stream = StreamId::Microphone;
    m.sampleCount = samples;
    for (std::size_t i = 0; i < samples; ++i)
        m.audio[i] = static_cast<std::int16_t>(i);
    return m;
}

}  // namespace

TEST(Process_HoldsAConversationWithTheHelper)
{
    std::wstring problem;
    std::unique_ptr<ProcessTransport> helper = Start("echo", &problem);
    CHECK(helper != nullptr);
    if (!helper)
        return;
    Watchdog watchdog(*helper);
    asr::MessageReader reader;
    asr::Message message;

    CHECK(Send(*helper, Hello()));
    CHECK(Receive(*helper, reader, &message));
    CHECK(message.type == asr::Type::Ready);

    CHECK(Send(*helper, AudioOf(100)));
    CHECK(Send(*helper, AudioOf(250)));
    for (const wchar_t *expected : {L"samples=100", L"samples=350"}) {
        CHECK(Receive(*helper, reader, &message));
        CHECK(message.type == asr::Type::Event);
        CHECK_EQ(std::wstring(message.event.text), std::wstring(expected));
    }

    asr::Message stop;
    stop.type = asr::Type::Stop;
    CHECK(Send(*helper, stop));
    CHECK(Receive(*helper, reader, &message));
    CHECK_EQ(std::wstring(message.event.text), std::wstring(L"bye"));
    CHECK(message.event.isFinal);
    CHECK(!Receive(*helper, reader, &message));         /* it exits: the stream ends */
    CHECK(WaitUntil([&] { return !helper->Running(); }));
}

TEST(Process_RunsAtBelowNormalPriority)
{
    std::wstring problem;
    std::unique_ptr<ProcessTransport> helper = Start("echo", &problem);
    CHECK(helper != nullptr);
    if (!helper)
        return;
    Watchdog watchdog(*helper);
    asr::MessageReader reader;
    asr::Message message;
    CHECK(Send(*helper, Hello()));
    CHECK(Receive(*helper, reader, &message));
    CHECK(message.text.find(L"priority=4000") != std::wstring::npos);      /* BELOW_NORMAL_PRIORITY_CLASS */
}

TEST(Process_CannotStartProcessesOfItsOwn)
{
    std::wstring problem;
    std::unique_ptr<ProcessTransport> helper = Start("spawn", &problem);
    CHECK(helper != nullptr);
    if (!helper)
        return;
    Watchdog watchdog(*helper);
    asr::MessageReader reader;
    asr::Message message;
    CHECK(Send(*helper, Hello()));
    CHECK(Receive(*helper, reader, &message));
    CHECK(message.text.find(L"spawn=blocked") != std::wstring::npos);     /* the job allows one process: itself */
}

TEST(Process_CloseKillsAHelperThatIgnoresIt)
{
    std::wstring problem;
    std::unique_ptr<ProcessTransport> helper = Start("hang", &problem);
    CHECK(helper != nullptr);
    if (!helper)
        return;
    CHECK(helper->Running());

    std::atomic<bool> readReturned{false};
    std::size_t got = 99;
    std::thread reader([&] {
        std::uint8_t bytes[16];
        got = helper->Read(bytes, sizeof(bytes));       /* waits for a helper that never speaks */
        readReturned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(!readReturned);

    helper->Close();
    CHECK(WaitUntil([&] { return readReturned.load(); }));
    reader.join();
    CHECK_EQ(got, std::size_t(0));
    CHECK(WaitUntil([&] { return !helper->Running(); }));
    helper->Close();                                    /* again: harmless */
}

TEST(Process_CloseFreesAWriteThatWaitsForAHelperThatDoesNotRead)
{
    std::wstring problem;
    std::unique_ptr<ProcessTransport> helper = Start("hang", &problem);
    CHECK(helper != nullptr);
    if (!helper)
        return;

    std::atomic<bool> writeReturned{false};
    bool wrote = true;
    std::thread writer([&] {
        const std::vector<std::uint8_t> block(32 * 1024, 7);
        for (int i = 0; i < 100 && wrote; ++i)          /* 3 MB: far more than the pipe holds */
            wrote = helper->Write(block.data(), block.size());
        writeReturned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(!writeReturned);                              /* stuck on a full pipe */

    helper->Close();
    CHECK(WaitUntil([&] { return writeReturned.load(); }));
    writer.join();
    CHECK(!wrote);
}

TEST(Process_ACrashedHelperEndsTheStream)
{
    std::wstring problem;
    std::unique_ptr<ProcessTransport> helper = Start("crash", &problem);
    CHECK(helper != nullptr);
    if (!helper)
        return;
    Watchdog watchdog(*helper);
    std::uint8_t bytes[16];
    CHECK_EQ(helper->Read(bytes, sizeof(bytes)), std::size_t(0));
    CHECK(WaitUntil([&] { return !helper->Running(); }));
    const std::vector<std::uint8_t> block(1024, 1);
    CHECK(!helper->Write(block.data(), block.size()));
}

TEST(Process_DroppingTheTransportKillsTheHelper)
{
    std::wstring problem;
    std::unique_ptr<ProcessTransport> helper = Start("hang", &problem);
    CHECK(helper != nullptr);
    if (!helper)
        return;
    const DWORD pid = helper->ProcessId();
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    CHECK(process != nullptr);
    CHECK(WaitForSingleObject(process, 0) == WAIT_TIMEOUT);

    helper.reset();
    CHECK(WaitForSingleObject(process, 5000) == WAIT_OBJECT_0);
    CloseHandle(process);
}

TEST(Process_RefusesWhatIsNotAFullPath)
{
    std::wstring problem;
    CHECK(ProcessTransport::Launch(L"audior_asr.exe", L"", ProcessTransport::Limits(), &problem) == nullptr);
    CHECK(!problem.empty());

    problem.clear();
    CHECK(ProcessTransport::Launch(L"..\\audior_asr.exe", L"", ProcessTransport::Limits(), &problem) == nullptr);
    CHECK(!problem.empty());
}

TEST(Process_ReportsAFileThatIsNotThere)
{
    std::wstring problem;
    CHECK(ProcessTransport::Launch(L"C:\\no\\such\\folder\\audior_asr.exe", L"", ProcessTransport::Limits(), &problem) ==
          nullptr);
    CHECK(problem.find(L"transcription engine") != std::wstring::npos);
}

TEST(Process_TheHostWorksWithARealProcess)
{
    TranscriptionHost::Options options;
    options.model = L"model";
    TranscriptionHost host(
        [](std::wstring *problem, const StopCheck &) -> std::unique_ptr<IAsrTransport> {
            return Start("echo", problem);
        },
        options);
    host.Start(1);
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Live; }));

    const std::vector<std::int16_t> audio(1600, 5);
    for (int i = 0; i < 5; ++i)
        host.Push(StreamId::Microphone, audio.data(), audio.size());
    std::wstring last;
    TranscriptEvent events[8];
    CHECK(WaitUntil([&] {
        const std::size_t n = host.TakeEvents(StreamId::Microphone, events, 8);
        for (std::size_t i = 0; i < n; ++i)
            last = events[i].text;
        return last == L"samples=8000";                 /* all of it arrived, in order, through real pipes */
    }));

    host.Stop();
    const std::size_t n = host.TakeEvents(StreamId::Microphone, events, 8);
    CHECK(n > 0);
    if (n > 0) {
        CHECK_EQ(std::wstring(events[n - 1].text), std::wstring(L"bye"));
        CHECK(events[n - 1].isFinal);
    }
    CHECK(host.Status(StreamId::Microphone).state == TranscriptState::Off);
}

TEST(Process_TheHostGivesUpOnAHelperThatCrashesAtOnce)
{
    TranscriptionHost::Options options;
    options.backoffMs = 5;
    options.maxRestarts = 1;
    TranscriptionHost host(
        [](std::wstring *problem, const StopCheck &) -> std::unique_ptr<IAsrTransport> {
            return Start("crash", problem);
        },
        options);
    host.Start(1);
    CHECK(WaitUntil([&] { return host.Status(StreamId::Microphone).state == TranscriptState::Failed; }));
    host.Stop();
}
