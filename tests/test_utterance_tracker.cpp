#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

#include "../src/core/utterance_tracker.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kMs = 16;                         /* samples per millisecond */

using Signal = std::vector<std::int16_t>;

/* A tone of the given loudness (RMS, in dBFS): stands in for speech. */
Signal Tone(int ms, double dbfs, double hertz = 300.0)
{
    const double amplitude = std::pow(10.0, dbfs / 20.0) * std::sqrt(2.0) * 32768.0;
    Signal s(static_cast<std::size_t>(ms) * kMs);
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<std::int16_t>(std::lround(amplitude * std::sin(2.0 * kPi * hertz * i / 16000.0)));
    return s;
}

Signal Silence(int ms) { return Signal(static_cast<std::size_t>(ms) * kMs, 0); }

/* Room noise of the given loudness. */
Signal Hiss(int ms, double dbfs)
{
    const double amplitude = std::pow(10.0, dbfs / 20.0) * std::sqrt(3.0) * 32768.0;
    std::uint32_t seed = 12345;
    Signal s(static_cast<std::size_t>(ms) * kMs);
    for (std::int16_t &v : s) {
        seed = seed * 1664525u + 1013904223u;
        v = static_cast<std::int16_t>(std::lround(amplitude * ((seed >> 8) / 8388608.0 - 1.0)));
    }
    return s;
}

Signal Join(std::initializer_list<Signal> parts)
{
    Signal all;
    for (const Signal &p : parts)
        all.insert(all.end(), p.begin(), p.end());
    return all;
}

/* What a step looked like. */
struct Seen {
    UtteranceTracker::Step::Kind kind;
    std::uint32_t                id;
    std::uint64_t                start;
    Signal                       audio;

    bool operator==(const Seen &o) const
    {
        return kind == o.kind && id == o.id && start == o.start && audio == o.audio;
    }
};

struct Run {
    UtteranceTracker  tracker;
    std::vector<Seen> seen;

    UtteranceTracker::Sink Sink()
    {
        return [this](const UtteranceTracker::Step &s) {
            seen.push_back({s.kind, s.id, s.startSample, Signal(s.audio, s.audio + s.count)});
        };
    }

    void Feed(const Signal &signal, std::size_t chunk, std::uint64_t firstSample = 0)
    {
        for (std::size_t at = 0; at < signal.size(); at += chunk)
            tracker.Add(signal.data() + at, std::min(chunk, signal.size() - at), firstSample + at, Sink());
    }

    std::vector<Seen> Finals() const
    {
        std::vector<Seen> finals;
        for (const Seen &s : seen) {
            if (s.kind == UtteranceTracker::Step::Kind::Final)
                finals.push_back(s);
        }
        return finals;
    }
};

}  // namespace

TEST(Tracker_SilenceAndRoomNoiseSayNothing)
{
    Run run;
    run.Feed(Join({Silence(5000), Hiss(10000, -50.0), Hiss(10000, -42.0), Hiss(10000, -45.0)}), 1600);
    CHECK(run.seen.empty());                            /* even a noisy room: the floor follows it */
}

TEST(Tracker_ASteadyNoiseThatSwitchesOnBecomesTheRoom)
{
    Run run;
    run.Feed(Join({Silence(1000), Hiss(60000, -38.0)}), 1600);
    CHECK(!run.seen.empty());                           /* at first it is something loud... */
    for (const Seen &s : run.seen)
        CHECK(s.start < 30 * 16000);                    /* ...but within seconds it is the room */
}

TEST(Tracker_AnUtteranceIsCutOutWithAMarginAroundIt)
{
    Run run;
    run.Feed(Join({Silence(1000), Tone(2000, -20.0), Silence(1500)}), 1600);

    const std::vector<Seen> finals = run.Finals();
    CHECK_EQ(finals.size(), std::size_t(1));
    if (finals.size() != 1)
        return;
    CHECK_EQ(finals[0].id, std::uint32_t(0));
    CHECK_EQ(finals[0].start, std::uint64_t(1000 * kMs - 240 * kMs));          /* 240 ms of pre-roll */
    const std::uint64_t end = finals[0].start + finals[0].audio.size();
    CHECK(end >= 3000 * kMs && end <= 3000 * kMs + 160 * kMs + 640);          /* the speech and a short tail, not the whole pause */
}

TEST(Tracker_PartialsComeWhileTheSpeechGoesOn)
{
    Run run;
    run.Feed(Join({Silence(1000), Tone(3500, -20.0), Silence(1500)}), 1600);

    std::vector<Seen> partials;
    for (const Seen &s : run.seen) {
        if (s.kind == UtteranceTracker::Step::Kind::Partial)
            partials.push_back(s);
    }
    const std::vector<Seen> finals = run.Finals();
    CHECK(partials.size() >= 2 && partials.size() <= 5);
    CHECK_EQ(finals.size(), std::size_t(1));
    if (finals.empty())
        return;
    for (std::size_t i = 0; i < partials.size(); ++i) {
        CHECK_EQ(partials[i].id, finals[0].id);
        CHECK_EQ(partials[i].start, finals[0].start);                           /* the same utterance, growing */
        CHECK(partials[i].audio.size() < finals[0].audio.size());
        CHECK(std::equal(partials[i].audio.begin(), partials[i].audio.end(), finals[0].audio.begin()));
        if (i > 0)
            CHECK(partials[i].audio.size() >= partials[i - 1].audio.size() + 1000 * kMs - 640);
    }
    CHECK(partials[0].audio.size() >= 800 * kMs);                               /* not before the first 0.8 s */
}

TEST(Tracker_TheOrderIsPartialsThenTheFinal)
{
    Run run;
    run.Feed(Join({Silence(500), Tone(2500, -20.0), Silence(1500)}), 800);
    CHECK(run.seen.size() >= 2);
    for (std::size_t i = 0; i + 1 < run.seen.size(); ++i)
        CHECK(run.seen[i].kind == UtteranceTracker::Step::Kind::Partial);
    CHECK(run.seen.back().kind == UtteranceTracker::Step::Kind::Final);
}

TEST(Tracker_APauseSeparatesUtterances)
{
    Run run;
    run.Feed(Join({Silence(500), Tone(1500, -20.0), Silence(1500), Tone(1500, -22.0), Silence(1500)}), 1600);
    const std::vector<Seen> finals = run.Finals();
    CHECK_EQ(finals.size(), std::size_t(2));
    if (finals.size() == 2) {
        CHECK_EQ(finals[0].id, std::uint32_t(0));
        CHECK_EQ(finals[1].id, std::uint32_t(1));
        CHECK(finals[1].start > finals[0].start + finals[0].audio.size());
    }
}

TEST(Tracker_AShortPauseDoesNotSplitAnUtterance)
{
    Run run;
    run.Feed(Join({Silence(500), Tone(1200, -20.0), Silence(300), Tone(1200, -20.0), Silence(1500)}), 1600);
    CHECK_EQ(run.Finals().size(), std::size_t(1));
}

TEST(Tracker_ABurstTooShortToBeSpeechIsDropped)
{
    for (int ms : {60, 100, 250}) {
        Run run;
        run.Feed(Join({Silence(800), Tone(ms, -20.0), Silence(2000)}), 1600);
        CHECK(run.seen.empty());
    }
    Run run;
    run.Feed(Join({Silence(800), Tone(400, -20.0), Silence(2000)}), 1600);
    CHECK_EQ(run.Finals().size(), std::size_t(1));
}

TEST(Tracker_ALongUtteranceIsCutAndTheNextBeginsWhereItEnded)
{
    Run run;
    run.Feed(Join({Silence(500), Tone(20000, -20.0), Silence(1500)}), 1600);
    const std::vector<Seen> finals = run.Finals();
    CHECK_EQ(finals.size(), std::size_t(2));
    if (finals.size() != 2)
        return;
    CHECK_EQ(finals[0].audio.size(), std::size_t(15000 * kMs));                /* the maximum */
    CHECK_EQ(finals[1].id, std::uint32_t(1));
    CHECK_EQ(finals[1].start, finals[0].start + finals[0].audio.size());       /* not a sample lost between them */
}

TEST(Tracker_ASpeakerLouderThanTheRoomIsFoundAtAnyNoiseLevel)
{
    Run run;
    run.Feed(Join({Hiss(5000, -45.0), Tone(2000, -20.0), Hiss(3000, -45.0)}), 1600);
    CHECK_EQ(run.Finals().size(), std::size_t(1));
}

TEST(Tracker_LostAudioEndsTheUtteranceThere)
{
    Run run;
    const Signal first = Join({Silence(500), Tone(1500, -20.0)});
    run.Feed(first, 1600);
    CHECK(run.seen.empty() || run.Finals().empty());                            /* still being spoken */

    const std::uint64_t resume = first.size() + 8000;                           /* half a second never arrived */
    run.Feed(Join({Silence(1500), Tone(1500, -20.0), Silence(1500)}), 1600, resume);
    const std::vector<Seen> finals = run.Finals();
    CHECK_EQ(finals.size(), std::size_t(2));
    if (finals.size() == 2) {
        CHECK(finals[0].start + finals[0].audio.size() <= first.size() + 160 * kMs);   /* only what was received */
        CHECK(finals[1].start >= resume);
        CHECK_EQ(finals[1].id, std::uint32_t(1));
    }
}

TEST(Tracker_FlushEndsAnUtteranceInProgress)
{
    Run run;
    run.Feed(Join({Silence(500), Tone(1500, -20.0)}), 1600);
    CHECK(run.Finals().empty());
    run.tracker.Flush(run.Sink());
    CHECK_EQ(run.Finals().size(), std::size_t(1));

    const std::size_t before = run.seen.size();
    run.tracker.Flush(run.Sink());                                              /* nothing is in progress now */
    CHECK_EQ(run.seen.size(), before);
}

TEST(Tracker_NumbersAreTheStreamsNotTheCallers)
{
    Run run;
    run.Feed(Join({Silence(1000), Tone(2000, -20.0), Silence(1500)}), 1600, 1000000);
    const std::vector<Seen> finals = run.Finals();
    CHECK_EQ(finals.size(), std::size_t(1));
    if (!finals.empty())
        CHECK_EQ(finals[0].start, std::uint64_t(1000000 + 1000 * kMs - 240 * kMs));
}

TEST(Tracker_ThePiecesTheAudioComesInDoNotMatter)
{
    const Signal signal = Join({Silence(500), Tone(2500, -20.0), Silence(1200), Tone(1800, -25.0), Silence(1500)});
    Run whole;
    whole.Feed(signal, signal.size());
    CHECK(whole.seen.size() >= 4);

    for (std::size_t chunk : {std::size_t(1), std::size_t(100), std::size_t(160), std::size_t(1600), std::size_t(7777)}) {
        Run run;
        run.Feed(signal, chunk);
        CHECK(run.seen == whole.seen);
    }
}
