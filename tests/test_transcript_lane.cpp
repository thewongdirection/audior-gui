#include <cwchar>
#include <string>

#include "../src/core/transcript_lane.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

TranscriptEvent Event(std::uint32_t utterance, const wchar_t *text, bool isFinal = false, double start = 0.0)
{
    TranscriptEvent e;
    e.utterance = utterance;
    e.isFinal = isFinal;
    e.startSeconds = start;
    e.SetText(text, std::wcslen(text));
    return e;
}

std::wstring TextAt(const TranscriptLane &lane, std::size_t index) { return lane.At(index).text; }

}  // namespace

TEST(Transcript_StartsEmpty)
{
    TranscriptLane lane;
    CHECK_EQ(lane.Count(), std::size_t(0));
    CHECK(!lane.Active());
    CHECK_NEAR(lane.Slide(), 0.0, 0.0);
}

TEST(Transcript_ANewUtteranceAddsALineAndSlidesIn)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"hello", false, 1.5));
    CHECK_EQ(lane.Count(), std::size_t(1));
    CHECK_EQ(TextAt(lane, 0), std::wstring(L"hello"));
    CHECK(!lane.At(0).isFinal);
    CHECK_NEAR(lane.At(0).startSeconds, 1.5, 0.0);
    CHECK_NEAR(lane.Slide(), 1.0, 0.0);
    CHECK(lane.Active());
}

TEST(Transcript_APartialReplacesItsLine)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"hello"));
    lane.Apply(Event(0, L"hello there"));
    CHECK_EQ(lane.Count(), std::size_t(1));
    CHECK_EQ(TextAt(lane, 0), std::wstring(L"hello there"));
    lane.Apply(Event(0, L"hello they"));                /* the last word is revised */
    CHECK_EQ(TextAt(lane, 0), std::wstring(L"hello they"));
    CHECK_EQ(lane.Count(), std::size_t(1));
}

TEST(Transcript_AFinalFixesTheLine)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"hello there"));
    lane.Apply(Event(0, L"Hello there.", true));
    CHECK(lane.At(0).isFinal);
    CHECK_EQ(TextAt(lane, 0), std::wstring(L"Hello there."));

    lane.Apply(Event(0, L"something else"));            /* a final line never changes */
    lane.Apply(Event(0, L"something else", true));
    CHECK_EQ(TextAt(lane, 0), std::wstring(L"Hello there."));
    CHECK_EQ(lane.Count(), std::size_t(1));
}

TEST(Transcript_LinesComeOldestFirst)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"one", true));
    lane.Apply(Event(1, L"two", true));
    lane.Apply(Event(2, L"three"));
    CHECK_EQ(lane.Count(), std::size_t(3));
    CHECK_EQ(TextAt(lane, 0), std::wstring(L"one"));
    CHECK_EQ(TextAt(lane, 1), std::wstring(L"two"));
    CHECK_EQ(TextAt(lane, 2), std::wstring(L"three"));
}

TEST(Transcript_OnlyANewLineRestartsTheSlide)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"hello"));
    lane.Advance(1.0);
    CHECK(!lane.Active());

    lane.Apply(Event(0, L"hello there"));               /* the same line grows: no slide */
    CHECK(!lane.Active());

    lane.Apply(Event(1, L"next"));
    CHECK_NEAR(lane.Slide(), 1.0, 0.0);
}

TEST(Transcript_TheSlideSettlesWithTime)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"hello"));
    lane.Advance(0.11);
    CHECK(lane.Slide() > 0.4 && lane.Slide() < 0.6);
    CHECK(lane.Active());
    lane.Advance(1.0);
    CHECK_NEAR(lane.Slide(), 0.0, 0.0);
    CHECK(!lane.Active());
}

TEST(Transcript_KeepsOnlyTheNewestLines)
{
    TranscriptLane lane;
    const std::uint32_t total = 3 * TranscriptLane::kMaxLines + 2;
    for (std::uint32_t i = 0; i < total; ++i)
        lane.Apply(Event(i, (L"line " + std::to_wstring(i)).c_str(), true));

    CHECK_EQ(lane.Count(), TranscriptLane::kMaxLines);
    CHECK_EQ(lane.At(0).utterance, total - static_cast<std::uint32_t>(TranscriptLane::kMaxLines));
    CHECK_EQ(lane.At(lane.Count() - 1).utterance, total - 1);
    CHECK_EQ(TextAt(lane, lane.Count() - 1), L"line " + std::to_wstring(total - 1));
}

TEST(Transcript_EmptyTextAddsNothing)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L""));
    lane.Apply(Event(1, L"", true));
    CHECK_EQ(lane.Count(), std::size_t(0));
    CHECK(!lane.Active());
}

TEST(Transcript_AnEmptyFinalTakesBackTheLiveLine)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"first", true));
    lane.Apply(Event(1, L"mumble"));
    lane.Apply(Event(1, L"", true));                    /* nothing was recognised after all */
    CHECK_EQ(lane.Count(), std::size_t(1));
    CHECK_EQ(TextAt(lane, 0), std::wstring(L"first"));

    lane.Apply(Event(2, L"more"));                      /* the id can be taken up again */
    CHECK_EQ(lane.Count(), std::size_t(2));
}

TEST(Transcript_AnEmptyFinalInTheMiddleJustClosesItsLine)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"first"));
    lane.Apply(Event(1, L"second"));                    /* 0 is still open when 1 begins */
    lane.Apply(Event(0, L"", true));
    CHECK_EQ(lane.Count(), std::size_t(2));
    CHECK(lane.At(0).isFinal);
    CHECK_EQ(TextAt(lane, 0), std::wstring(L"first"));
    CHECK(!lane.At(1).isFinal);
}

TEST(Transcript_LateNewsOfAScrolledAwayLineIsIgnored)
{
    TranscriptLane lane;
    const std::uint32_t total = 2 * TranscriptLane::kMaxLines;
    for (std::uint32_t i = 0; i < total; ++i)
        lane.Apply(Event(i, L"line", true));
    lane.Advance(1.0);

    lane.Apply(Event(2, L"a late revision", true));     /* utterance 2 left the ring long ago */
    CHECK_EQ(lane.Count(), TranscriptLane::kMaxLines);
    CHECK_EQ(lane.At(lane.Count() - 1).utterance, total - 1);
    CHECK(!lane.Active());                              /* and nothing slid */
}

TEST(Transcript_AnEmptyPartialKeepsTheText)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"hello"));
    lane.Apply(Event(0, L""));
    CHECK_EQ(TextAt(lane, 0), std::wstring(L"hello"));
}

TEST(Transcript_VersionChangesOnlyWithTheText)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"hello"));
    const std::uint32_t first = lane.At(0).version;
    lane.Apply(Event(0, L"hello"));                     /* the same again */
    CHECK_EQ(lane.At(0).version, first);
    lane.Apply(Event(0, L"hello", true));               /* final with the same text */
    CHECK_EQ(lane.At(0).version, first);

    lane.Apply(Event(1, L"hello"));                     /* another line, though the same words */
    CHECK(lane.At(1).version != first);
}

TEST(Transcript_SettleMakesTheLiveLineFinal)
{
    TranscriptLane lane;
    lane.Settle();                                      /* nothing to settle: no harm */
    lane.Apply(Event(0, L"one", true));
    lane.Apply(Event(1, L"still talking"));
    CHECK(!lane.At(1).isFinal);
    lane.Settle();
    CHECK(lane.At(1).isFinal);
    CHECK_EQ(TextAt(lane, 1), std::wstring(L"still talking"));
}

TEST(Transcript_ResetEmptiesTheLane)
{
    TranscriptLane lane;
    lane.Apply(Event(0, L"hello"));
    const std::uint32_t before = lane.At(0).version;
    lane.Reset();
    CHECK_EQ(lane.Count(), std::size_t(0));
    CHECK(!lane.Active());

    lane.Apply(Event(0, L"hello"));                     /* ids start over with a recording */
    CHECK_EQ(lane.Count(), std::size_t(1));
    CHECK(lane.At(0).version != before);                /* but a version is never reused */
}

TEST(Transcript_ATextWithoutATerminatorIsReadSafely)
{
    TranscriptEvent e;
    e.utterance = 0;
    for (wchar_t &c : e.text)
        c = L'a';                                       /* the whole buffer, no terminator */
    TranscriptLane lane;
    lane.Apply(e);
    CHECK_EQ(lane.Count(), std::size_t(1));
    CHECK_EQ(std::wcslen(lane.At(0).text), kTranscriptChars - 1);
}

TEST(TranscriptEvent_ShortTextIsCopiedWhole)
{
    TranscriptEvent e;
    e.SetText(L"hello", 5);
    CHECK_EQ(std::wstring(e.text), std::wstring(L"hello"));
}

TEST(TranscriptEvent_LongTextIsCutAtAWordBoundary)
{
    std::wstring text;
    while (text.size() < 2 * kTranscriptChars)
        text += L"word ";
    TranscriptEvent e;
    e.SetText(text.c_str(), text.size());

    const std::wstring cut = e.text;
    CHECK(cut.size() < kTranscriptChars);
    CHECK_EQ(cut.back(), L'…');
    CHECK_EQ(cut[cut.size() - 2], L'd');                /* the last word is whole */
    CHECK_EQ(cut.compare(0, 4, L"word"), 0);
}

TEST(TranscriptEvent_OneHugeWordIsCutHard)
{
    const std::wstring text(2 * kTranscriptChars, L'x');
    TranscriptEvent e;
    e.SetText(text.c_str(), text.size());
    CHECK_EQ(std::wcslen(e.text), kTranscriptChars - 1);
    CHECK_EQ(e.text[kTranscriptChars - 2], L'…');
}

TEST(TranscriptEvent_TextThatJustFitsIsKept)
{
    const std::wstring text(kTranscriptChars - 1, L'y');
    TranscriptEvent e;
    e.SetText(text.c_str(), text.size());
    CHECK_EQ(std::wstring(e.text), text);
}
