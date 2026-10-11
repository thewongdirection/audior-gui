#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "../src/core/asr_protocol.h"
#include "test_support.h"

using namespace audiorgui;
using namespace audiorgui::asr;

namespace {

std::vector<std::uint8_t> Frame(const Message &m)
{
    std::vector<std::uint8_t> bytes(kMaxFrame);
    bytes.resize(Encode(m, bytes.data(), bytes.size()));
    return bytes;
}

/* Feeds `bytes` in pieces of `piece` and returns what the reader makes of them: the messages' types
 * (in `types`) and whether it ended up Invalid. */
bool ReadAll(const std::vector<std::uint8_t> &bytes, std::size_t piece, Message *last, std::vector<Type> *types)
{
    MessageReader reader;
    for (std::size_t at = 0; at < bytes.size();) {
        const std::size_t n = std::min(piece, bytes.size() - at);
        std::size_t used = 0;
        while (used < n) {
            used += reader.Append(bytes.data() + at + used, n - used);
            for (;;) {
                const MessageReader::Result r = reader.Next(last);
                if (r == MessageReader::Result::Invalid)
                    return false;
                if (r == MessageReader::Result::NeedMore)
                    break;
                types->push_back(last->type);
            }
        }
        at += n;
    }
    return true;
}

Message ReadOne(const std::vector<std::uint8_t> &bytes)
{
    Message m;
    std::vector<Type> types;
    CHECK(ReadAll(bytes, bytes.size(), &m, &types));
    CHECK_EQ(types.size(), std::size_t(1));
    return m;
}

Message EventMessage(StreamId stream, std::uint32_t id, bool isFinal, double start, const wchar_t *text)
{
    Message m;
    m.type = Type::Event;
    m.stream = stream;
    m.event.utterance = id;
    m.event.isFinal = isFinal;
    m.event.startSeconds = start;
    m.event.SetText(text, std::wcslen(text));
    return m;
}

/* Overwrites a byte of an encoded frame: for building malformed ones. */
std::vector<std::uint8_t> With(std::vector<std::uint8_t> bytes, std::size_t at, std::uint8_t value)
{
    bytes[at] = value;
    return bytes;
}

bool IsInvalid(const std::vector<std::uint8_t> &bytes)
{
    Message m;
    std::vector<Type> types;
    return !ReadAll(bytes, bytes.size(), &m, &types);
}

}  // namespace

TEST(Asr_HelloRoundTrips)
{
    Message m;
    m.type = Type::Hello;
    m.streams = 3;
    m.text = L"C:\\Models\\ggml-small.en-q5_1.bin";
    const Message r = ReadOne(Frame(m));
    CHECK(r.type == Type::Hello);
    CHECK_EQ(r.version, kVersion);
    CHECK_EQ(int(r.streams), 3);
    CHECK(r.language == SpeechLanguage::Auto);
    CHECK_EQ(r.text, m.text);
    CHECK(r.vadModel.empty());                          /* none unless one is given */
    CHECK(r.gpu);                                       /* the card is used unless told not to */

    m.gpu = false;
    CHECK(!ReadOne(Frame(m)).gpu);
    m.gpu = true;
    m.language = SpeechLanguage::English;
    m.vadModel = L"C:\\Models\\ggml-silero-v5.1.2.bin";
    const Message again = ReadOne(Frame(m));
    CHECK(again.language == SpeechLanguage::English);
    CHECK_EQ(again.text, m.text);
    CHECK_EQ(again.vadModel, m.vadModel);
}

TEST(Asr_ANonsensicalGpuFlagIsRefused)
{
    Message hello;
    hello.type = Type::Hello;
    hello.text = L"m";
    const std::vector<std::uint8_t> bytes = Frame(hello);
    CHECK(!IsInvalid(With(bytes, kFrameHeader + 4, 0)));                /* after the version, streams and language */
    CHECK(!IsInvalid(With(bytes, kFrameHeader + 4, 1)));
    CHECK(IsInvalid(With(bytes, kFrameHeader + 4, 2)));
    CHECK(IsInvalid(With(bytes, kFrameHeader + 4, 255)));
}

TEST(Asr_AHelloWithoutItsSecondPathIsRefused)
{
    Message hello;
    hello.type = Type::Hello;
    hello.text = L"m";
    std::vector<std::uint8_t> bytes = Frame(hello);
    bytes.resize(bytes.size() - sizeof(std::uint16_t));                 /* cut the (empty) second string off */
    const std::uint32_t payload = static_cast<std::uint32_t>(bytes.size() - kFrameHeader);
    std::memcpy(bytes.data(), &payload, sizeof(payload));
    CHECK(IsInvalid(bytes));
}

TEST(Asr_AnUnknownLanguageIsRefused)
{
    Message hello;
    hello.type = Type::Hello;
    hello.text = L"m";
    CHECK(!IsInvalid(With(Frame(hello), kFrameHeader + 3, 1)));         /* after the version and the streams */
    CHECK(IsInvalid(With(Frame(hello), kFrameHeader + 3, 2)));
    CHECK(IsInvalid(With(Frame(hello), kFrameHeader + 3, 255)));
}

TEST(Asr_AudioRoundTrips)
{
    Message m;
    m.type = Type::Audio;
    m.stream = StreamId::System;
    m.firstSample = 0x0123456789ABull;
    m.sampleCount = kMaxAudioSamples;
    for (std::size_t i = 0; i < m.sampleCount; ++i)
        m.audio[i] = static_cast<std::int16_t>(static_cast<int>(i) * 37 - 20000);
    const std::vector<std::uint8_t> bytes = Frame(m);
    CHECK(bytes.size() <= kMaxFrame);

    const Message r = ReadOne(bytes);
    CHECK(r.type == Type::Audio);
    CHECK(r.stream == StreamId::System);
    CHECK_EQ(r.firstSample, m.firstSample);
    CHECK_EQ(r.sampleCount, m.sampleCount);
    CHECK(std::memcmp(r.audio, m.audio, m.sampleCount * sizeof(std::int16_t)) == 0);
}

TEST(Asr_AnEmptyAudioMessageIsAllowed)
{
    Message m;
    m.type = Type::Audio;
    m.sampleCount = 0;
    CHECK_EQ(ReadOne(Frame(m)).sampleCount, std::size_t(0));
}

TEST(Asr_StopAndReadyRoundTrip)
{
    Message stop;
    stop.type = Type::Stop;
    CHECK_EQ(Frame(stop).size(), kFrameHeader);
    CHECK(ReadOne(Frame(stop)).type == Type::Stop);

    Message ready;
    ready.type = Type::Ready;
    ready.text = L"small.en · Vulkan (RTX 5080)";
    CHECK_EQ(ReadOne(Frame(ready)).text, ready.text);
}

TEST(Asr_StatusRoundTrips)
{
    Message m;
    m.type = Type::Status;
    m.stream = StreamId::System;
    m.status.state = TranscriptState::Live;
    m.status.lagSeconds = 1.25f;
    const Message r = ReadOne(Frame(m));
    CHECK(r.stream == StreamId::System);
    CHECK(r.status.state == TranscriptState::Live);
    CHECK_NEAR(r.status.lagSeconds, 1.25, 1e-6);

    m.status.state = TranscriptState::CatchingUp;
    CHECK(ReadOne(Frame(m)).status.state == TranscriptState::CatchingUp);
}

TEST(Asr_EventRoundTrips)
{
    const Message r = ReadOne(Frame(EventMessage(StreamId::Microphone, 41, true, 12.5, L"Hello there, world.")));
    CHECK(r.type == Type::Event);
    CHECK(r.stream == StreamId::Microphone);
    CHECK_EQ(r.event.utterance, std::uint32_t(41));
    CHECK(r.event.isFinal);
    CHECK_NEAR(r.event.startSeconds, 12.5, 0.0);
    CHECK_EQ(std::wstring(r.event.text), std::wstring(L"Hello there, world."));
}

TEST(Asr_ProblemRoundTrips)
{
    Message m;
    m.type = Type::Problem;
    m.stream = StreamId::System;
    m.text = L"The model could not be loaded.";
    Message r = ReadOne(Frame(m));
    CHECK(!r.all);
    CHECK(r.stream == StreamId::System);
    CHECK_EQ(r.text, m.text);

    m.all = true;
    r = ReadOne(Frame(m));
    CHECK(r.all);
}

TEST(Asr_TextOfTheLongestAllowedSizeSurvives)
{
    const std::wstring text(kTranscriptChars - 1, L'x');
    const Message r = ReadOne(Frame(EventMessage(StreamId::Microphone, 0, false, 0.0, text.c_str())));
    CHECK_EQ(std::wstring(r.event.text), text);
}

TEST(Asr_AnUnterminatedEventTextIsCutToWhatTheParserAccepts)
{
    Message m;
    m.type = Type::Event;
    for (wchar_t &c : m.event.text)
        c = L'z';                                       /* the whole buffer, no terminator */
    const Message r = ReadOne(Frame(m));
    CHECK_EQ(std::wcslen(r.event.text), kTranscriptChars - 1);
}

TEST(Asr_EncodeRefusesWhatDoesNotFit)
{
    Message m;
    m.type = Type::Audio;
    m.sampleCount = kMaxAudioSamples;
    std::uint8_t small[100];
    CHECK_EQ(Encode(m, small, sizeof(small)), std::size_t(0));

    m.sampleCount = kMaxAudioSamples + 1;               /* more than one message may carry */
    std::vector<std::uint8_t> big(2 * kMaxFrame);
    CHECK_EQ(Encode(m, big.data(), big.size()), std::size_t(0));

    Message hello;
    hello.type = Type::Hello;
    hello.text = std::wstring(70000, L'a');             /* a string length is a u16 */
    CHECK_EQ(Encode(hello, big.data(), big.size()), std::size_t(0));
}

TEST(Asr_MessagesCanComeInAnyPieces)
{
    Message hello;
    hello.type = Type::Hello;
    hello.streams = 1;
    hello.text = L"model";
    std::vector<std::uint8_t> stream = Frame(hello);
    const std::vector<std::uint8_t> event = Frame(EventMessage(StreamId::System, 1, false, 3.0, L"words"));
    stream.insert(stream.end(), event.begin(), event.end());
    Message stop;
    stop.type = Type::Stop;
    const std::vector<std::uint8_t> end = Frame(stop);
    stream.insert(stream.end(), end.begin(), end.end());

    for (std::size_t piece : {std::size_t(1), std::size_t(3), std::size_t(7), stream.size()}) {
        Message last;
        std::vector<Type> types;
        CHECK(ReadAll(stream, piece, &last, &types));
        CHECK_EQ(types.size(), std::size_t(3));
        if (types.size() == 3)
            CHECK(types[0] == Type::Hello && types[1] == Type::Event && types[2] == Type::Stop);
    }
}

TEST(Asr_AnIncompleteFrameWaitsForMore)
{
    const std::vector<std::uint8_t> frame = Frame(EventMessage(StreamId::Microphone, 0, true, 1.0, L"complete"));
    for (std::size_t cut = 0; cut < frame.size(); ++cut) {          /* every proper prefix */
        MessageReader reader;
        reader.Append(frame.data(), cut);
        Message m;
        CHECK(reader.Next(&m) == MessageReader::Result::NeedMore);
    }
}

TEST(Asr_ManyMessagesFlowThroughTheFixedBuffer)
{
    std::vector<std::uint8_t> stream;
    const std::size_t count = 100;                      /* 100 x 409 bytes: more than the reader's buffer holds */
    for (std::size_t i = 0; i < count; ++i) {
        Message m;
        m.type = Type::Audio;
        m.sampleCount = 200;
        m.firstSample = i;
        const std::vector<std::uint8_t> frame = Frame(m);
        stream.insert(stream.end(), frame.begin(), frame.end());
    }
    Message last;
    std::vector<Type> types;
    CHECK(ReadAll(stream, 1000, &last, &types));
    CHECK_EQ(types.size(), count);
    CHECK_EQ(last.firstSample, std::uint64_t(count - 1));
}

/* ---- what is refused ---- */

TEST(Asr_ADeclaredSizeBeyondTheLimitIsRefused)
{
    Message stop;
    stop.type = Type::Stop;
    std::vector<std::uint8_t> bytes = Frame(stop);
    const std::uint32_t huge = static_cast<std::uint32_t>(kMaxPayload + 1);
    std::memcpy(bytes.data(), &huge, sizeof(huge));
    CHECK(IsInvalid(bytes));

    const std::uint32_t everything = 0xFFFFFFFFu;
    std::memcpy(bytes.data(), &everything, sizeof(everything));
    CHECK(IsInvalid(bytes));
}

TEST(Asr_AnUnknownTypeIsRefused)
{
    Message stop;
    stop.type = Type::Stop;
    const std::vector<std::uint8_t> bytes = Frame(stop);
    CHECK(IsInvalid(With(bytes, 4, 0)));
    CHECK(IsInvalid(With(bytes, 4, 9)));
    CHECK(IsInvalid(With(bytes, 4, 255)));
}

TEST(Asr_ABadStreamIsRefused)
{
    const std::vector<std::uint8_t> event = Frame(EventMessage(StreamId::Microphone, 0, false, 0.0, L"x"));
    CHECK(IsInvalid(With(event, kFrameHeader, kStreamCount)));
    CHECK(IsInvalid(With(event, kFrameHeader, 200)));

    Message audio;
    audio.type = Type::Audio;
    audio.sampleCount = 4;
    CHECK(IsInvalid(With(Frame(audio), kFrameHeader, 2)));

    Message problem;
    problem.type = Type::Problem;
    problem.text = L"x";
    CHECK(!IsInvalid(With(Frame(problem), kFrameHeader, kAllStreams)));    /* only here is "all" allowed */
    CHECK(IsInvalid(With(Frame(problem), kFrameHeader, 7)));
}

TEST(Asr_AnOddAudioSizeIsRefused)
{
    Message audio;
    audio.type = Type::Audio;
    audio.sampleCount = 4;
    std::vector<std::uint8_t> bytes = Frame(audio);
    bytes.push_back(0);                                 /* one byte too many... */
    const std::uint32_t size = static_cast<std::uint32_t>(bytes.size() - kFrameHeader);
    std::memcpy(bytes.data(), &size, sizeof(size));     /* ...and the header agrees with it */
    CHECK(IsInvalid(bytes));
}

TEST(Asr_TooMuchAudioInOneMessageIsRefused)
{
    std::vector<std::uint8_t> bytes(kFrameHeader + 9 + (kMaxAudioSamples + 1) * 2, 0);
    const std::uint32_t size = static_cast<std::uint32_t>(bytes.size() - kFrameHeader);
    std::memcpy(bytes.data(), &size, sizeof(size));
    bytes[4] = static_cast<std::uint8_t>(Type::Audio);
    CHECK(IsInvalid(bytes));
}

TEST(Asr_ANonsensicalStatusIsRefused)
{
    Message m;
    m.type = Type::Status;
    m.status.state = TranscriptState::Live;
    m.status.lagSeconds = 0.5f;
    const std::vector<std::uint8_t> ok = Frame(m);
    CHECK(!IsInvalid(ok));
    CHECK(!IsInvalid(With(ok, kFrameHeader + 1, 4)));                   /* the last state there is */
    CHECK(IsInvalid(With(ok, kFrameHeader + 1, 5)));                    /* one that does not exist */
    CHECK(IsInvalid(With(ok, kFrameHeader + 1, 9)));

    m.status.lagSeconds = std::numeric_limits<float>::quiet_NaN();
    CHECK(IsInvalid(Frame(m)));
    m.status.lagSeconds = -1.0f;
    CHECK(IsInvalid(Frame(m)));
    m.status.lagSeconds = std::numeric_limits<float>::infinity();
    CHECK(IsInvalid(Frame(m)));
}

TEST(Asr_ANonsensicalEventIsRefused)
{
    const std::vector<std::uint8_t> ok = Frame(EventMessage(StreamId::Microphone, 0, false, 1.0, L"fine"));
    CHECK(!IsInvalid(ok));
    CHECK(IsInvalid(With(ok, kFrameHeader + 5, 2)));                    /* final flag other than 0 or 1 */
    CHECK(IsInvalid(Frame(EventMessage(StreamId::Microphone, 0, false, -1.0, L"x"))));
    CHECK(IsInvalid(Frame(EventMessage(StreamId::Microphone, 0, false, std::nan(""), L"x"))));
}

TEST(Asr_AStringLongerThanItsPayloadIsRefused)
{
    Message problem;
    problem.type = Type::Problem;
    problem.text = L"abc";
    std::vector<std::uint8_t> bytes = Frame(problem);
    bytes[kFrameHeader + 1] = 50;                       /* it claims 50 characters; there are 3 */
    CHECK(IsInvalid(bytes));

    bytes = Frame(problem);
    bytes[kFrameHeader + 1] = 0xFF;
    bytes[kFrameHeader + 2] = 0xFF;                     /* 65535 characters */
    CHECK(IsInvalid(bytes));
}

TEST(Asr_ATextLongerThanALineIsRefused)
{
    /* Built by hand: the encoder would cut it. */
    const std::size_t chars = kTranscriptChars;         /* one more than an event may carry */
    std::vector<std::uint8_t> bytes;
    auto put = [&](const void *p, std::size_t n) {
        const std::uint8_t *b = static_cast<const std::uint8_t *>(p);
        bytes.insert(bytes.end(), b, b + n);
    };
    const std::uint8_t stream = 0, isFinal = 0;
    const std::uint32_t id = 0;
    const double start = 0.0;
    const std::uint16_t length = static_cast<std::uint16_t>(chars);
    const std::wstring text(chars, L'a');
    const std::uint32_t size = static_cast<std::uint32_t>(1 + 4 + 1 + 8 + 2 + chars * sizeof(wchar_t));
    put(&size, 4);
    bytes.push_back(static_cast<std::uint8_t>(Type::Event));
    put(&stream, 1);
    put(&id, 4);
    put(&isFinal, 1);
    put(&start, 8);
    put(&length, 2);
    put(text.data(), chars * sizeof(wchar_t));
    CHECK(IsInvalid(bytes));
}

TEST(Asr_TrailingBytesInAFrameAreRefused)
{
    Message m;
    m.type = Type::Status;
    m.status.state = TranscriptState::Live;
    std::vector<std::uint8_t> bytes = Frame(m);
    bytes.push_back(0);
    const std::uint32_t size = static_cast<std::uint32_t>(bytes.size() - kFrameHeader);
    std::memcpy(bytes.data(), &size, sizeof(size));
    CHECK(IsInvalid(bytes));

    Message stop;
    stop.type = Type::Stop;
    bytes = Frame(stop);
    bytes.push_back(0);
    const std::uint32_t one = 1;
    std::memcpy(bytes.data(), &one, sizeof(one));       /* a Stop with a payload */
    CHECK(IsInvalid(bytes));
}

TEST(Asr_InvalidIsFinal)
{
    Message stop;
    stop.type = Type::Stop;
    std::vector<std::uint8_t> bytes = With(Frame(stop), 4, 99);
    const std::vector<std::uint8_t> good = Frame(stop);
    bytes.insert(bytes.end(), good.begin(), good.end());        /* a good message after the bad one */

    MessageReader reader;
    CHECK_EQ(reader.Append(bytes.data(), bytes.size()), bytes.size());
    Message m;
    CHECK(reader.Next(&m) == MessageReader::Result::Invalid);
    CHECK(reader.Next(&m) == MessageReader::Result::Invalid);
    reader.Append(good.data(), good.size());
    CHECK(reader.Next(&m) == MessageReader::Result::Invalid);
}

TEST(Asr_RandomBytesNeverCrashTheParser)
{
    std::uint64_t seed = 12345;
    auto next = [&] {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>(seed >> 33);
    };
    Message m;
    for (int round = 0; round < 3000; ++round) {
        MessageReader reader;
        std::vector<std::uint8_t> bytes(1 + next() % 300);
        for (std::uint8_t &b : bytes)
            b = static_cast<std::uint8_t>(next());
        if (round % 3 == 0 && bytes.size() > kFrameHeader) {            /* a plausible header, a random body */
            const std::uint32_t size = static_cast<std::uint32_t>(bytes.size() - kFrameHeader);
            std::memcpy(bytes.data(), &size, sizeof(size));
            bytes[4] = static_cast<std::uint8_t>(1 + next() % 19);
        }
        for (std::size_t at = 0; at < bytes.size();) {
            const std::size_t n = std::min<std::size_t>(1 + next() % 40, bytes.size() - at);
            std::size_t used = 0;
            while (used < n)
                used += reader.Append(bytes.data() + at + used, n - used);
            at += n;
            while (reader.Next(&m) == MessageReader::Result::Message) {
            }
        }
    }
    CHECK(true);                                        /* getting here is the test */
}
