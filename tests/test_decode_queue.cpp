#include "../src/core/decode_queue.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

DecodeJob Job(StreamId stream, std::uint32_t id, bool isFinal, std::size_t samples = 4)
{
    DecodeJob job;
    job.stream = stream;
    job.id = id;
    job.isFinal = isFinal;
    job.audio.assign(samples, static_cast<std::int16_t>(id));
    return job;
}

constexpr StreamId kMic = StreamId::Microphone, kSys = StreamId::System;

}  // namespace

TEST(DecodeQueue_StartsEmpty)
{
    DecodeQueue queue;
    DecodeJob job;
    CHECK(queue.Empty());
    CHECK(!queue.Pop(&job));
    CHECK_EQ(queue.Dropped(), std::size_t(0));
}

TEST(DecodeQueue_AFinalComesBeforeAPartial)
{
    DecodeQueue queue;
    queue.Push(Job(kMic, 1, false));
    queue.Push(Job(kSys, 3, true));
    DecodeJob job;
    CHECK(queue.Pop(&job));
    CHECK(job.isFinal && job.stream == kSys);
    CHECK(queue.Pop(&job));
    CHECK(!job.isFinal && job.stream == kMic);
    CHECK(queue.Empty());
}

TEST(DecodeQueue_FinalsKeepTheirOrderAcrossStreams)
{
    DecodeQueue queue;
    queue.Push(Job(kMic, 1, true));
    queue.Push(Job(kSys, 7, true));
    queue.Push(Job(kMic, 2, true));
    DecodeJob job;
    for (std::uint32_t expected : {1u, 7u, 2u}) {
        CHECK(queue.Pop(&job));
        CHECK_EQ(job.id, expected);
    }
}

TEST(DecodeQueue_ANewerPartialReplacesTheWaitingOne)
{
    DecodeQueue queue;
    queue.Push(Job(kMic, 1, false, 10));
    queue.Push(Job(kMic, 1, false, 20));                /* the same phrase, further on */
    queue.Push(Job(kSys, 5, false, 30));

    DecodeJob job;
    CHECK(queue.Pop(&job));
    CHECK(job.stream == kMic);
    CHECK_EQ(job.audio.size(), std::size_t(20));
    CHECK(queue.Pop(&job));
    CHECK(job.stream == kSys);
    CHECK(!queue.Pop(&job));
}

TEST(DecodeQueue_PartialsGoOldestFirst)
{
    DecodeQueue queue;
    queue.Push(Job(kSys, 5, false));
    queue.Push(Job(kMic, 1, false));
    DecodeJob job;
    CHECK(queue.Pop(&job));
    CHECK(job.stream == kSys);
}

TEST(DecodeQueue_AFinalMakesItsOwnPartialPointless)
{
    DecodeQueue queue;
    queue.Push(Job(kMic, 4, false));
    queue.Push(Job(kMic, 4, true));
    DecodeJob job;
    CHECK(queue.Pop(&job));
    CHECK(job.isFinal);
    CHECK(!queue.Pop(&job));                            /* the draft is gone */
}

TEST(DecodeQueue_AFinalKeepsTheNextPhrasesPartial)
{
    DecodeQueue queue;
    queue.Push(Job(kMic, 5, false));                    /* the next phrase has begun already */
    queue.Push(Job(kMic, 4, true));
    DecodeJob job;
    CHECK(queue.Pop(&job));
    CHECK(job.isFinal && job.id == 4);
    CHECK(queue.Pop(&job));
    CHECK(!job.isFinal && job.id == 5);
}

TEST(DecodeQueue_OnlyAFewFinalsWaitAndTheOldestIsLost)
{
    DecodeQueue queue;
    for (std::uint32_t id = 1; id <= DecodeQueue::kMaxFinals + 2; ++id)
        queue.Push(Job(kMic, id, true));
    CHECK_EQ(queue.Dropped(), std::size_t(2));

    DecodeJob job;
    for (std::uint32_t expected = 3; expected <= DecodeQueue::kMaxFinals + 2; ++expected) {
        CHECK(queue.Pop(&job));
        CHECK_EQ(job.id, expected);
    }
    CHECK(!queue.Pop(&job));
}

TEST(DecodeQueue_SaysWhichPhraseWasLost)
{
    DecodeQueue queue;
    DecodeQueue::Lost lost;
    for (std::uint32_t id = 1; id <= DecodeQueue::kMaxFinals; ++id) {
        DecodeJob job = Job(id % 2 ? kMic : kSys, id, true);
        job.startSample = 1000 * id;
        CHECK(!queue.Push(std::move(job), &lost));      /* there is still room */
    }
    CHECK(queue.Push(Job(kMic, 99, true), &lost));      /* now the oldest one goes */
    CHECK_EQ(lost.id, std::uint32_t(1));
    CHECK(lost.stream == kMic);
    CHECK_EQ(lost.startSample, std::uint64_t(1000));

    CHECK(!queue.Push(Job(kMic, 100, false)));          /* a partial never costs anyone their place */
    CHECK(queue.Push(Job(kSys, 101, true)));            /* (and the answer is optional) */
}

TEST(DecodeQueue_AJobIsMovedNotCopied)
{
    DecodeQueue queue;
    DecodeJob in = Job(kMic, 1, true, 100000);
    const std::int16_t *data = in.audio.data();
    queue.Push(std::move(in));
    DecodeJob out;
    CHECK(queue.Pop(&out));
    CHECK(out.audio.data() == data);                    /* the audio was never copied on its way through */
}
