#include <string>

#include "../src/core/asr_text.h"
#include "test_support.h"

using namespace audiorgui;

TEST(AsrText_Utf8Decodes)
{
    CHECK_EQ(WideFromUtf8(""), std::wstring());
    CHECK_EQ(WideFromUtf8("plain ascii"), std::wstring(L"plain ascii"));
    CHECK_EQ(WideFromUtf8("caf\xC3\xA9"), std::wstring(L"café"));                  /* two bytes */
    CHECK_EQ(WideFromUtf8("\xE2\x80\x99"), std::wstring(L"’"));                    /* three bytes: a curly quote */
    CHECK_EQ(WideFromUtf8("\xE6\x97\xA5\xE6\x9C\xAC"), std::wstring(L"日本"));  /* two CJK characters */
}

TEST(AsrText_Utf8DecodesCharactersAboveTheBasicPlane)
{
    const std::wstring wide = WideFromUtf8("\xF0\x9F\x98\x80");                         /* U+1F600 */
    static_assert(sizeof(wchar_t) == 2, "Windows' wchar_t is UTF-16");
    CHECK_EQ(wide.size(), std::size_t(2));                                              /* a surrogate pair */
    CHECK_EQ(int(wide[0]), 0xD83D);
    CHECK_EQ(int(wide[1]), 0xDE00);
}

TEST(AsrText_BadUtf8BecomesReplacementCharacters)
{
    CHECK_EQ(WideFromUtf8("a\xFFz"), std::wstring(L"a�z"));                         /* never a valid byte */
    CHECK_EQ(WideFromUtf8("a\x80z"), std::wstring(L"a�z"));                         /* a stray continuation byte */
    CHECK_EQ(WideFromUtf8("a\xC3"), std::wstring(L"a�"));                           /* cut short */
    CHECK_EQ(WideFromUtf8("\xC0\xAF"), std::wstring(L"��"));                  /* an overlong "/" */
    CHECK_EQ(WideFromUtf8("\xED\xA0\x80"), std::wstring(L"���"));        /* an encoded surrogate */
    CHECK_EQ(WideFromUtf8("\xE2\x28\xA1"), std::wstring(L"�(�"));             /* a broken sequence */
}

TEST(AsrText_PlainSpeechIsKept)
{
    CHECK_EQ(CleanTranscript(L"Hello there, world."), std::wstring(L"Hello there, world."));
    CHECK_EQ(CleanTranscript(L"It's 5 o'clock — really."), std::wstring(L"It's 5 o'clock — really."));
    CHECK_EQ(CleanTranscript(L"日本語"), std::wstring(L"日本語"));       /* other scripts */
}

TEST(AsrText_WhiteSpaceIsTidied)
{
    CHECK_EQ(CleanTranscript(L"  hello \t\n  there  "), std::wstring(L"hello there"));
    CHECK_EQ(CleanTranscript(L"a b"), std::wstring(L"a b"));                       /* a no-break space */
}

TEST(AsrText_AnnotationsAreRemoved)
{
    CHECK_EQ(CleanTranscript(L"[BLANK_AUDIO]"), std::wstring());
    CHECK_EQ(CleanTranscript(L"(music)"), std::wstring());
    CHECK_EQ(CleanTranscript(L"*sighs*"), std::wstring());
    CHECK_EQ(CleanTranscript(L" [Music] "), std::wstring());
    CHECK_EQ(CleanTranscript(L"Hello [pause] there"), std::wstring(L"Hello there"));
    CHECK_EQ(CleanTranscript(L"So (laughs) that is it."), std::wstring(L"So that is it."));
    CHECK_EQ(CleanTranscript(L"[A] and [B]"), std::wstring(L"and"));
}

TEST(AsrText_BracketsWithoutWordsAreKept)
{
    CHECK_EQ(CleanTranscript(L"(1) first point"), std::wstring(L"(1) first point"));
    CHECK_EQ(CleanTranscript(L"see [2] and [3]"), std::wstring(L"see [2] and [3]"));
    CHECK_EQ(CleanTranscript(L"5 * 3 * 2"), std::wstring(L"5 * 3 * 2"));
}

TEST(AsrText_AnUnclosedOrOverlongBracketIsLeftAlone)
{
    CHECK_EQ(CleanTranscript(L"Open (never closed"), std::wstring(L"Open (never closed"));
    const std::wstring wordy = L"(" + std::wstring(60, L'x') + L")";                   /* too long to be an annotation */
    CHECK_EQ(CleanTranscript(wordy), wordy);
}

/* ---- phrases that go round in circles ---- */

namespace {

std::wstring Collapse(const std::wstring &text, bool *found = nullptr)
{
    bool local = false;
    std::wstring out = RemoveRepeatedPhrases(text, found ? found : &local);
    return out;
}

std::wstring Times(const std::wstring &phrase, int n)
{
    std::wstring all;
    for (int i = 0; i < n; ++i)
        all += (i ? L" " : L"") + phrase;
    return all;
}

}  // namespace

TEST(AsrText_ALoopedPhraseKeepsOneCopy)
{
    bool found = false;
    CHECK_EQ(Collapse(Times(L"And so my fellow Americans, ask for help.", 22), &found),
             std::wstring(L"And so my fellow Americans, ask for help."));
    CHECK(found);
    CHECK_EQ(Collapse(L"Hello there. Hello there. Hello there.", &found), std::wstring(L"Hello there."));
    CHECK(found);
}

TEST(AsrText_TheLoopMayFollowRealText)
{
    bool found = false;
    CHECK_EQ(Collapse(L"Fine. And then " + Times(L"and then", 4), &found), std::wstring(L"Fine. And then"));
    CHECK(found);
    CHECK_EQ(Collapse(L"I said " + Times(L"go", 7) + L" now", &found), std::wstring(L"I said go now"));
    CHECK(found);
}

TEST(AsrText_RepeatsAreComparedWithoutCaseOrPunctuation)
{
    bool found = false;
    CHECK_EQ(Collapse(L"Okay, okay. OKAY! okay; Okay okay okay", &found), std::wstring(L"Okay,"));
    CHECK(found);
}

TEST(AsrText_PeopleDoRepeatThemselvesALittle)
{
    bool found = true;
    CHECK_EQ(Collapse(L"ha ha ha ha ha", &found), std::wstring(L"ha ha ha ha ha"));         /* five of one word */
    CHECK(!found);
    CHECK_EQ(Collapse(L"Hello there. Hello there.", &found), std::wstring(L"Hello there. Hello there."));
    CHECK(!found);
    CHECK_EQ(Collapse(L"no no no no", &found), std::wstring(L"no no no no"));
    CHECK(!found);
}

TEST(AsrText_OrdinaryTextIsLeftAlone)
{
    bool found = true;
    const std::wstring text = L"And so my fellow Americans, ask not what your country can do for you, ask what you can do for your country.";
    CHECK_EQ(Collapse(text, &found), text);
    CHECK(!found);
    CHECK_EQ(Collapse(L"", &found), std::wstring());
    CHECK(!found);
    CHECK_EQ(Collapse(L"one", &found), std::wstring(L"one"));
}

TEST(AsrText_NothingButNotesAndPunctuationIsEmpty)
{
    CHECK_EQ(CleanTranscript(L"♪ ♪"), std::wstring());
    CHECK_EQ(CleanTranscript(L"..."), std::wstring());
    CHECK_EQ(CleanTranscript(L"♪ la la ♪"), std::wstring(L"♪ la la ♪"));   /* it has words */
    CHECK_EQ(CleanTranscript(L""), std::wstring());
    CHECK_EQ(CleanTranscript(L"�"), std::wstring());
}
