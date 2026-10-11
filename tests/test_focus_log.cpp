#include "hardware/focus_log.h"
#include "test_support.h"

namespace {

/* `count` looks, one every 0.1 s from `from`, all of them held or none. */
void Look(FocusLog &log, const char *where, double from, int count, bool held, long long idleMs = 9000, bool button = false)
{
    for (int i = 0; i < count; ++i)
        log.Sample(where, from + 0.1 * i, held, idleMs, button);
}

bool Contains(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

}  // namespace

TEST(FocusLog_NeverHeldIsJustACount)
{
    FocusLog log;
    Look(log, "T1", 0.0, 50, false);
    CHECK_EQ(log.Samples(), 50);
    CHECK_EQ(log.Held(), 0);
    CHECK(log.Episodes().empty());
    CHECK_EQ(log.Summary(), std::string("0 of 50 samples"));
}

TEST(FocusLog_LooksInARowAreOneEpisodeThatRemembersItsStart)
{
    FocusLog log;
    Look(log, "T1", 0.0, 30, false, 9000);
    log.Sample("T1", 3.0, true, 200, true);             /* the start: input 200 ms ago, a button down */
    Look(log, "T1", 3.1, 19, true, 0, false);           /* the rest: later looks do not change the start */
    Look(log, "T1", 5.0, 10, false);

    CHECK_EQ(log.Held(), 20);
    CHECK_EQ(log.Samples(), 60);
    CHECK_EQ(log.Episodes().size(), std::size_t(1));
    const FocusEpisode &e = log.Episodes()[0];
    CHECK_EQ(e.where, std::string("T1"));
    CHECK_NEAR(e.start, 3.0, 1e-9);
    CHECK_NEAR(e.seconds, 2.0, 1e-9);
    CHECK_EQ(e.idleMs, 200LL);
    CHECK(e.buttonDown);
    CHECK_EQ(e.samples, 20);
}

TEST(FocusLog_AGapStartsAnotherEpisode)
{
    FocusLog log;
    Look(log, "T3", 1.0, 5, true, 100);
    Look(log, "T3", 1.5, 3, false);
    Look(log, "T3", 1.8, 4, true, 7000);
    CHECK_EQ(log.Episodes().size(), std::size_t(2));
    CHECK_EQ(log.Episodes()[0].samples, 5);
    CHECK_EQ(log.Episodes()[1].samples, 4);
    CHECK_NEAR(log.Episodes()[1].start, 1.8, 1e-9);
    CHECK_EQ(log.Episodes()[1].idleMs, 7000LL);
    CHECK_EQ(log.Held(), 9);
}

TEST(FocusLog_EndingARecordingClosesTheEpisode)
{
    /* A window that is foreground when its recording ends and again at the start of the next recording
     * (another program instance) is two episodes, not one. */
    FocusLog log;
    Look(log, "T1", 0.0, 10, true);
    log.EndRecording();
    log.EndRecording();                                 /* harmless twice */
    Look(log, "T2", 0.0, 10, true);
    CHECK_EQ(log.Episodes().size(), std::size_t(2));
    CHECK_EQ(log.Episodes()[0].where, std::string("T1"));
    CHECK_EQ(log.Episodes()[1].where, std::string("T2"));
    CHECK_EQ(log.Held(), 20);
}

TEST(FocusLog_TellsAClickFromAWindowThatCameForwardByItself)
{
    FocusEpisode e;
    e.idleMs = 300;                                     /* input just before: someone acting */
    CHECK(Contains(FocusLog::Cause(e), "someone clicking or typing"));
    e.idleMs = FocusLog::kPersonWithinMs;
    CHECK(Contains(FocusLog::Cause(e), "someone clicking or typing"));

    e.idleMs = 5200;                                    /* nobody at the keyboard or mouse */
    CHECK(Contains(FocusLog::Cause(e), "not a click"));
    CHECK(Contains(FocusLog::Cause(e), "5.2 s"));
    e.idleMs = FocusLog::kNoInputMs;
    CHECK(Contains(FocusLog::Cause(e), "not a click"));

    e.idleMs = 3000;                                    /* in between */
    CHECK(Contains(FocusLog::Cause(e), "unclear"));

    e.idleMs = 20000;                                   /* a held button is a click whatever the input clock says */
    e.buttonDown = true;
    CHECK(Contains(FocusLog::Cause(e), "a click"));
    CHECK(!Contains(FocusLog::Cause(e), "not a click"));

    e.buttonDown = false;
    e.idleMs = -1;
    CHECK(Contains(FocusLog::Cause(e), "unknown"));
}

TEST(FocusLog_SummaryNamesWhereWhenAndWhy)
{
    FocusLog log;
    Look(log, "T1", 0.0, 34, false);
    Look(log, "T1", 3.4, 10, true, 6100);
    const std::string one = log.Summary();
    CHECK(Contains(one, "10 of 44 samples, in 1 episode: "));
    CHECK(Contains(one, "T1 at 3.4 s for 1.0 s"));
    CHECK(Contains(one, "no input for 6.1 s"));
    CHECK(!Contains(one, "and "));
}

TEST(FocusLog_SummarySpellsOutOnlyTheFirstFewEpisodes)
{
    FocusLog log;
    for (int i = 0; i < 5; ++i) {
        Look(log, "T5", i * 1.0, 2, true, 100);
        Look(log, "T5", i * 1.0 + 0.2, 2, false);
    }
    CHECK_EQ(log.Episodes().size(), std::size_t(5));
    const std::string text = log.Summary();
    CHECK(Contains(text, "in 5 episodes: "));
    CHECK(Contains(text, "; and 2 more"));
    CHECK(Contains(text, "T5 at 2.0 s"));               /* the third is spelled out */
    CHECK(!Contains(text, "T5 at 3.0 s"));              /* the fourth is not */
}
