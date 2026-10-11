/*
 * asr_protocol.h - What the recorder and its transcription helper say to each
 * other over the helper's standard input and output.
 *
 * The helper is a separate process (it may crash, or hold the GPU), so the
 * conversation is a plain byte stream of framed messages:
 *
 *     [u32 payload bytes][u8 type][payload]        little-endian throughout
 *
 *   recorder -> helper                       helper -> recorder
 *     Hello   streams, language, models        Ready   what it runs (model, device)
 *     Audio   16 kHz mono samples of a         Status  a stream's state and lag
 *             stream, with the number of       Event   an utterance's text so far
 *             its first sample                 Problem why a stream (or all) stopped
 *     Stop    no more audio: finish up
 *
 * Strings are UTF-16: [u16 characters][characters].  The helper's output is
 * not trusted: MessageReader checks every length and field and refuses
 * anything malformed, so a broken or hostile helper can only end the
 * conversation.  Everything here is plain C++, tested without a process.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "transcript.h"
#include "types.h"

namespace audiorgui::asr {

constexpr std::uint16_t kVersion = 4;
constexpr int           kSampleRate = 16000;            /* the audio the helper receives */

/* The biggest payload either side sends; an Audio message carries at most
 * kMaxAudioSamples of them. */
constexpr std::size_t kMaxPayload      = 16 * 1024;
constexpr std::size_t kMaxAudioSamples = 1600;          /* 100 ms */
constexpr std::size_t kFrameHeader     = 5;
constexpr std::size_t kMaxFrame        = kFrameHeader + kMaxPayload;

enum class Type : std::uint8_t {
    Hello = 1, Audio = 2, Stop = 3,                     /* recorder -> helper */
    Ready = 16, Status = 17, Event = 18, Problem = 19,  /* helper -> recorder */
};

constexpr std::uint8_t kAllStreams = 0xFF;              /* a Problem about every stream */

/* One message, whichever way it travels.  Only the fields of its type count. */
struct Message {
    Type            type = Type::Stop;
    StreamId        stream = StreamId::Microphone;      /* Audio, Status, Event, Problem (see `all`) */
    bool            all = false;                        /* Problem: about every stream               */
    std::uint8_t    streams = 0;                        /* Hello: a bit per StreamId to transcribe   */
    SpeechLanguage  language = SpeechLanguage::Auto;    /* Hello                                     */
    bool            gpu = true;                         /* Hello: use the graphics card if there is one (else only the processor) */
    std::uint16_t   version = kVersion;                 /* Hello                                     */
    std::wstring    text;                               /* Hello: the model's path; Ready: what runs;
                                                           Problem: why                              */
    std::wstring    vadModel;                           /* Hello: the voice-activity model's path, or none */
    std::uint64_t   firstSample = 0;                    /* Audio: the stream's sample number of audio[0]  */
    std::size_t     sampleCount = 0;                    /* Audio: how many of `audio` count          */
    std::int16_t    audio[kMaxAudioSamples];            /* Audio: 16 kHz mono                        */
    TranscriptStatus status;                            /* Status */
    TranscriptEvent  event;                             /* Event */
};

/* Writes `message` as one frame into `out`.  Returns its size, or 0 if it does
 * not fit in `capacity` bytes or breaks a limit (too many samples, an overlong
 * text...). */
std::size_t Encode(const Message &message, std::uint8_t *out, std::size_t capacity);

/* Turns a byte stream back into messages, in a buffer of fixed size.  Bytes are
 * fed in as they arrive (in any pieces); Next() hands out the messages they
 * complete. */
class MessageReader {
public:
    enum class Result { NeedMore, Message, Invalid };

    /* Takes up to `count` bytes; returns how many it took (fewer than `count`
     * only when its buffer is full: call Next() to make room). */
    std::size_t Append(const void *data, std::size_t count);

    /* The next complete message, if any.  Invalid is final: the stream is
     * corrupt and nothing more is read. */
    Result Next(Message *message);

private:
    std::uint8_t buffer_[2 * kMaxFrame];
    std::size_t  begin_ = 0, end_ = 0;
    bool         invalid_ = false;
};

}  // namespace audiorgui::asr
