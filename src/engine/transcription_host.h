/*
 * transcription_host.h - The recorder's side of live transcription: feeds a
 * transcription helper the audio of the streams and collects the text.
 *
 *   capture thread --Push()--> per-stream audio queue --writer--> helper
 *                                                                   |
 *   UI thread <--TakeEvents()-- per-stream event queue <--reader----+
 *
 * The helper is a separate process behind an IAsrTransport (the process pipes
 * in production, an in-memory fake in the tests).  The host is built so that
 * nothing the helper does can hurt a recording:
 *
 *   - Push() never blocks and never fails: the audio queue keeps the newest
 *     few seconds and drops the oldest, so memory is bounded and a stuck helper
 *     costs transcript text, not audio;
 *   - a helper that dies or breaks the protocol is restarted a few times (with
 *     a growing pause) and then given up on: the stream's status becomes Failed
 *     and says why;
 *   - nothing here throws into, or waits on, the capture threads.
 *
 * One supervising thread starts the helper, writes to it and restarts it; a
 * reader thread parses what it says.  See asr_protocol.h for the conversation.
 */
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/asr_protocol.h"
#include "../core/lag_governor.h"
#include "../core/transcript.h"
#include "speech_tap.h"

namespace audiorgui {

/* A conversation with one running helper. */
class IAsrTransport {
public:
    virtual ~IAsrTransport() = default;

    /* Sends all `count` bytes, waiting while the helper is busy; false once the
     * helper is gone. */
    virtual bool Write(const void *data, std::size_t count) = 0;

    /* Waits for bytes from the helper and returns how many; 0 once the helper
     * has closed its end or after Close(). */
    virtual std::size_t Read(void *buffer, std::size_t capacity) = 0;

    /* Ends the conversation and the helper, and frees a Read or Write that is
     * waiting.  May be called from any thread, and more than once. */
    virtual void Close() = 0;
};

/* True once the host has been asked to stop. */
using StopCheck = std::function<bool()>;

/* Starts a helper; null (with `*problem` saying why) if it cannot.  It runs on
 * the host's own thread and may take a while (checking a model reads all of it),
 * so it should give up, returning null, as soon as `stopping()` is true. */
using TransportFactory = std::function<std::unique_ptr<IAsrTransport>(std::wstring *problem, const StopCheck &stopping)>;

class TranscriptionHost final : public ISpeechSink {
public:
    /* A smaller model to change to, and how to start a helper that can use it. */
    struct Fallback {
        std::wstring     model;
        TransportFactory factory;
    };

    struct Options {
        std::wstring model;                         /* what the helper should load (sent in Hello) */
        std::wstring vadModel;                      /* the voice-activity model, if any (sent in Hello, too) */
        std::vector<Fallback> fallbacks;            /* if a stream stays hopelessly behind (see
                                                       StepDownPolicy), the next of these takes over,
                                                       without using up a restart                  */
        SpeechLanguage language = SpeechLanguage::Auto;     /* and what to take the speech for (Hello) */
        bool         gpu = true;                    /* and whether it may use the graphics card (Hello) */
        int          maxRestarts    = 3;            /* in a row; then the streams are Failed       */
        int          backoffMs      = 1000;         /* before the first restart; doubles after     */
        int          healthyMs      = 60000;        /* a helper that ran this long before dying
                                                       does not count towards maxRestarts          */
        int          readyTimeoutMs = 60000;        /* a helper that is not Ready by then is stuck */
        int          flushMs        = 3000;         /* how long Stop() waits for the last words    */
        double       stepDownHoldSeconds = StepDownPolicy::kHoldSeconds;    /* how long "hopeless" lasts  */
    };

    TranscriptionHost(TransportFactory factory, Options options);
    ~TranscriptionHost();                           /* stops, if still running */

    TranscriptionHost(const TranscriptionHost &) = delete;
    TranscriptionHost &operator=(const TranscriptionHost &) = delete;

    /* Begins transcribing the streams whose bit (1 << StreamIndex) is set.
     * Returns at once; whatever goes wrong shows in Status().  Once only. */
    void Start(std::uint8_t streams);

    /* No more audio: the helper finishes the words it has (for up to flushMs),
     * and the threads end.  Events can be taken afterwards. */
    void Stop();

    /* ISpeechSink: capture thread. */
    void Push(StreamId stream, const std::int16_t *samples, std::size_t count) override;

    /* UI thread. */
    TranscriptStatus Status(StreamId stream) const;
    std::wstring     Problem(StreamId stream) const;
    std::size_t      TakeEvents(StreamId stream, TranscriptEvent *out, std::size_t capacity);

private:
    /* The newest few seconds of a stream's audio, numbered by absolute sample. */
    class SampleQueue {
    public:
        SampleQueue();
        void        Push(const std::int16_t *samples, std::size_t count);
        std::size_t Read(std::int16_t *out, std::size_t maximum, std::uint64_t *firstSample);

    private:
        std::mutex                mutex_;
        std::vector<std::int16_t> data_;
        std::size_t               head_ = 0, used_ = 0;
        std::uint64_t             oldest_ = 0;          /* the absolute number of the oldest sample held */
    };

    /* The newest events not yet taken. */
    class EventQueue {
    public:
        EventQueue();
        void        Push(const TranscriptEvent &event);
        std::size_t Take(TranscriptEvent *out, std::size_t capacity);

    private:
        std::mutex                   mutex_;
        std::vector<TranscriptEvent> events_;
        std::size_t                  head_ = 0, used_ = 0;
    };

    struct Lane {
        std::unique_ptr<SampleQueue> audio;             /* only for a stream that is transcribed */
        std::unique_ptr<EventQueue>  events;
        TranscriptStatus status;                        /* guarded by stateMutex_ */
        std::wstring     problem;                       /* guarded by stateMutex_ */
        std::uint32_t    base = 0;                      /* added to the helper's utterance numbers */
        std::uint32_t    next = 0;                      /* one more than the highest seen, incl. base */
    };

    void Run();
    bool WriteLoop(IAsrTransport &transport);
    void ReadLoop(IAsrTransport *transport);
    bool Handle(const asr::Message &message);
    const TransportFactory &CurrentFactory() const { return stage_ == 0 ? factory_ : options_.fallbacks[stage_ - 1].factory; }
    const std::wstring &CurrentModel() const { return stage_ == 0 ? options_.model : options_.fallbacks[stage_ - 1].model; }
    bool StepRequested();
    bool TakeStepRequest();
    bool Wanted(StreamId stream) const { return (streams_ & (1u << StreamIndex(stream))) != 0; }
    void SetStatus(StreamId stream, const TranscriptStatus &status);
    void Fail(StreamId stream, const std::wstring &problem);
    void FailAll(const std::wstring &problem);
    void SetAllStarting();
    void SetAllOff();
    bool SleepUnlessStopped(int milliseconds);
    bool StopRequested();

    TransportFactory factory_;
    Options          options_;
    std::size_t      stage_ = 0;                        /* 0: the model asked for; n: fallbacks[n - 1]. The
                                                           supervisor changes it only between two helpers. */
    StepDownPolicy   stepDown_[kStreamCount];           /* the reader thread only */
    bool             started_ = false;                  /* Start() has been given streams (once only) */
    std::uint8_t     streams_ = 0;
    Lane             lanes_[kStreamCount];

    bool                    smaller_ = false;           /* a fallback model is running (guarded by stateMutex_) */
    mutable std::mutex      stateMutex_;                /* statuses and problems */
    std::mutex              controlMutex_;              /* the flags below       */
    std::condition_variable control_;
    bool                    stop_ = false;
    bool                    runDone_ = false;           /* the supervising thread has finished */
    bool                    readerDone_ = false;
    bool                    ready_ = false;             /* the current helper said Ready */
    bool                    stepRequested_ = false;   /* the current helper is too slow: change the model */
    IAsrTransport          *current_ = nullptr;         /* the helper in use, for Stop() to cut loose */
    std::thread             thread_;
};

}  // namespace audiorgui
