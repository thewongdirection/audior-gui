#include "asr_protocol.h"

#include <cmath>
#include <cstring>

#include "asr_languages.h"

namespace audiorgui::asr {

namespace {

/* Writes into a fixed buffer; once anything does not fit (or a limit is
 * broken) every later write is ignored and ok() stays false. */
class Writer {
public:
    Writer(std::uint8_t *out, std::size_t capacity) : out_(out), capacity_(capacity) {}

    template <typename T>
    void Put(T value) { Bytes(&value, sizeof(T)); }

    void Bytes(const void *data, std::size_t count)
    {
        if (!ok_ || count > capacity_ - size_) {
            ok_ = false;
            return;
        }
        std::memcpy(out_ + size_, data, count);
        size_ += count;
    }

    void String(const wchar_t *text, std::size_t length)
    {
        if (length > 0xFFFF) {
            ok_ = false;
            return;
        }
        Put(static_cast<std::uint16_t>(length));
        Bytes(text, length * sizeof(wchar_t));
    }

    bool        ok() const { return ok_; }
    std::size_t size() const { return size_; }

private:
    std::uint8_t *out_;
    std::size_t   capacity_, size_ = 0;
    bool          ok_ = true;
};

/* Reads from a fixed payload; reading past its end makes ok() false for good. */
class Reader {
public:
    Reader(const std::uint8_t *data, std::size_t size) : data_(data), size_(size) {}

    template <typename T>
    T Get()
    {
        T value{};
        Bytes(&value, sizeof(T));
        return value;
    }

    bool Bytes(void *out, std::size_t count)
    {
        if (!ok_ || count > size_ - pos_) {
            ok_ = false;
            return false;
        }
        std::memcpy(out, data_ + pos_, count);
        pos_ += count;
        return true;
    }

    /* A string of at most `maxChars` characters. */
    bool String(std::size_t maxChars, std::wstring *out)
    {
        const std::size_t length = Get<std::uint16_t>();
        if (!ok_ || length > maxChars || length * sizeof(wchar_t) > size_ - pos_) {
            ok_ = false;
            return false;
        }
        out->resize(length);
        if (length)
            std::memcpy(&(*out)[0], data_ + pos_, length * sizeof(wchar_t));
        pos_ += length * sizeof(wchar_t);
        return true;
    }

    bool ok() const { return ok_; }
    bool done() const { return ok_ && pos_ == size_; }
    std::size_t left() const { return size_ - pos_; }

private:
    const std::uint8_t *data_;
    std::size_t         size_, pos_ = 0;
    bool                ok_ = true;
};

bool ValidStream(std::uint8_t s) { return s < kStreamCount; }

bool Decode(Type type, const std::uint8_t *payload, std::size_t size, Message *m)
{
    Reader r(payload, size);
    m->type = type;
    switch (type) {
    case Type::Hello: {
        m->version = r.Get<std::uint16_t>();
        m->streams = r.Get<std::uint8_t>();
        const std::uint8_t language = r.Get<std::uint8_t>();
        const std::uint8_t gpu = r.Get<std::uint8_t>();
        const std::uint8_t engine = r.Get<std::uint8_t>();
        if (!r.ok() || !ValidSpeechLanguage(language) || gpu > 1 ||
            engine > static_cast<std::uint8_t>(AsrEngine::Parakeet))
            return false;
        m->language = static_cast<SpeechLanguage>(language);
        m->gpu = gpu != 0;
        m->engine = static_cast<AsrEngine>(engine);
        return r.String(size / sizeof(wchar_t), &m->text) && r.String(size / sizeof(wchar_t), &m->vadModel) && r.done();
    }
    case Type::Audio: {
        const std::uint8_t stream = r.Get<std::uint8_t>();
        m->firstSample = r.Get<std::uint64_t>();
        const std::size_t bytes = r.left();
        if (!r.ok() || !ValidStream(stream) || bytes % sizeof(std::int16_t) != 0 ||
            bytes > kMaxAudioSamples * sizeof(std::int16_t))
            return false;
        m->stream = static_cast<StreamId>(stream);
        m->sampleCount = bytes / sizeof(std::int16_t);
        return r.Bytes(m->audio, bytes) && r.done();
    }
    case Type::Stop:
        return size == 0;
    case Type::Ready:
        return r.String(size / sizeof(wchar_t), &m->text) && r.done();
    case Type::Status: {
        const std::uint8_t stream = r.Get<std::uint8_t>();
        const std::uint8_t state = r.Get<std::uint8_t>();
        const float lag = r.Get<float>();
        if (!r.done() || !ValidStream(stream) || state > static_cast<std::uint8_t>(TranscriptState::CatchingUp) ||
            !std::isfinite(lag) || lag < 0.0f || lag > 1e6f)
            return false;
        m->stream = static_cast<StreamId>(stream);
        m->status.state = static_cast<TranscriptState>(state);
        m->status.lagSeconds = lag;
        return true;
    }
    case Type::Event: {
        const std::uint8_t stream = r.Get<std::uint8_t>();
        TranscriptEvent &e = m->event;
        e.utterance = r.Get<std::uint32_t>();
        const std::uint8_t isFinal = r.Get<std::uint8_t>();
        e.startSeconds = r.Get<double>();
        std::wstring text;
        if (!r.String(kTranscriptChars - 1, &text) || !r.done() || !ValidStream(stream) || isFinal > 1 ||
            !std::isfinite(e.startSeconds) || e.startSeconds < 0.0)
            return false;
        m->stream = static_cast<StreamId>(stream);
        e.isFinal = isFinal == 1;
        e.SetText(text.c_str(), text.size());
        return true;
    }
    case Type::Problem: {
        const std::uint8_t stream = r.Get<std::uint8_t>();
        if (!r.ok() || (stream != kAllStreams && !ValidStream(stream)) ||
            !r.String(size / sizeof(wchar_t), &m->text) || !r.done())
            return false;
        m->all = stream == kAllStreams;
        m->stream = m->all ? StreamId::Microphone : static_cast<StreamId>(stream);
        return true;
    }
    }
    return false;
}

}  // namespace

std::size_t Encode(const Message &m, std::uint8_t *out, std::size_t capacity)
{
    Writer w(out, capacity);
    w.Put<std::uint32_t>(0);                            /* the payload size, filled in below */
    w.Put(static_cast<std::uint8_t>(m.type));

    switch (m.type) {
    case Type::Hello:
        w.Put(m.version);
        w.Put(m.streams);
        w.Put(static_cast<std::uint8_t>(m.language));
        w.Put(static_cast<std::uint8_t>(m.gpu ? 1 : 0));
        w.Put(static_cast<std::uint8_t>(m.engine));
        w.String(m.text.c_str(), m.text.size());
        w.String(m.vadModel.c_str(), m.vadModel.size());
        break;
    case Type::Audio:
        if (m.sampleCount > kMaxAudioSamples) {
            return 0;
        }
        w.Put(static_cast<std::uint8_t>(m.stream));
        w.Put(m.firstSample);
        w.Bytes(m.audio, m.sampleCount * sizeof(std::int16_t));
        break;
    case Type::Stop:
        break;
    case Type::Ready:
        w.String(m.text.c_str(), m.text.size());
        break;
    case Type::Status:
        w.Put(static_cast<std::uint8_t>(m.stream));
        w.Put(static_cast<std::uint8_t>(m.status.state));
        w.Put(m.status.lagSeconds);
        break;
    case Type::Event: {
        std::size_t length = 0;
        while (length < kTranscriptChars - 1 && m.event.text[length] != L'\0')     /* what Decode accepts */
            ++length;
        w.Put(static_cast<std::uint8_t>(m.stream));
        w.Put(m.event.utterance);
        w.Put(static_cast<std::uint8_t>(m.event.isFinal ? 1 : 0));
        w.Put(m.event.startSeconds);
        w.String(m.event.text, length);
        break;
    }
    case Type::Problem:
        w.Put(m.all ? kAllStreams : static_cast<std::uint8_t>(m.stream));
        w.String(m.text.c_str(), m.text.size());
        break;
    }

    const std::size_t payload = w.size() - kFrameHeader;
    if (!w.ok() || payload > kMaxPayload)
        return 0;
    const std::uint32_t size = static_cast<std::uint32_t>(payload);
    std::memcpy(out, &size, sizeof(size));
    return w.size();
}

std::size_t MessageReader::Append(const void *data, std::size_t count)
{
    if (invalid_)
        return count;                                   /* nothing more is read from a corrupt stream */
    if (begin_ > 0 && end_ + count > sizeof(buffer_)) {
        std::memmove(buffer_, buffer_ + begin_, end_ - begin_);
        end_ -= begin_;
        begin_ = 0;
    }
    const std::size_t take = count < sizeof(buffer_) - end_ ? count : sizeof(buffer_) - end_;
    std::memcpy(buffer_ + end_, data, take);
    end_ += take;
    return take;
}

MessageReader::Result MessageReader::Next(Message *message)
{
    if (invalid_)
        return Result::Invalid;
    const std::size_t available = end_ - begin_;
    if (available < kFrameHeader)
        return Result::NeedMore;

    std::uint32_t size;
    std::memcpy(&size, buffer_ + begin_, sizeof(size));
    if (size > kMaxPayload) {
        invalid_ = true;
        return Result::Invalid;
    }
    if (available < kFrameHeader + size)
        return Result::NeedMore;

    const Type type = static_cast<Type>(buffer_[begin_ + 4]);
    if (!Decode(type, buffer_ + begin_ + kFrameHeader, size, message)) {
        invalid_ = true;
        return Result::Invalid;
    }
    begin_ += kFrameHeader + size;
    if (begin_ == end_)
        begin_ = end_ = 0;
    return Result::Message;
}

}  // namespace audiorgui::asr
