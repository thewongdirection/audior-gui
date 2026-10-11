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

TEST(AsrText_ALongPhraseSaidTwiceInARowKeepsOneCopyButIsNotALoop)
{
    bool found = true;
    CHECK_EQ(Collapse(L"We meet at the station at nine. We meet at the station at nine.", &found),
             std::wstring(L"We meet at the station at nine."));
    CHECK(!found);                                      /* a draft with this is still shown, once */
    CHECK_EQ(Collapse(L"Well. We meet at the station. We meet at the station. Then we walk.", &found),
             std::wstring(L"Well. We meet at the station. Then we walk."));
    CHECK(!found);
    CHECK_EQ(Collapse(L"we meet at the station, WE MEET AT THE STATION!", &found), std::wstring(L"we meet at the station,"));
    CHECK(!found);
    /* three or more is a loop, as before */
    CHECK_EQ(Collapse(Times(L"We meet at the station at nine.", 3), &found), std::wstring(L"We meet at the station at nine."));
    CHECK(found);
}

TEST(AsrText_AWholeLongSentenceRepeatedAndACutShortLastCopyKeepOneCopy)
{
    bool found = true;
    const std::wstring sentence = L"Today the weather is mild, the river is calm, and the mountain road is open.";      /* 15 words */
    CHECK_EQ(Collapse(sentence + L" " + sentence, &found), sentence);
    CHECK(!found);
    CHECK_EQ(Collapse(Times(sentence, 4), &found), sentence);
    CHECK(found);

    /* what the model writes when it runs out of room in the middle of a copy */
    CHECK_EQ(Collapse(sentence + L" Today the weather is mild, the river is", &found), sentence);
    CHECK(!found);
    CHECK_EQ(Collapse(sentence + L" " + sentence + L" Today the weather is mild, the", &found), sentence);
    CHECK(!found);
    CHECK_EQ(Collapse(L"Fine. " + sentence + L" Today the weather is mild, the river is", &found), L"Fine. " + sentence);
    CHECK(!found);
}

TEST(AsrText_ASentenceThatOnlyBeginsLikeTheOneBeforeIsLeftAlone)
{
    bool found = true;
    const std::wstring text = L"We went to the old market on Monday and we went to the old harbour on Tuesday.";
    CHECK_EQ(Collapse(text, &found), text);
    CHECK(!found);
    const std::wstring four = L"I think we should go home and I think we should go now.";             /* four words in common after a gap */
    CHECK_EQ(Collapse(four, &found), four);
    CHECK(!found);
}

TEST(AsrText_PeopleDoRepeatThemselvesALittle)
{
    bool found = true;
    CHECK_EQ(Collapse(L"ha ha ha ha ha", &found), std::wstring(L"ha ha ha ha ha"));         /* five of one word */
    CHECK(!found);
    CHECK_EQ(Collapse(L"Hello there. Hello there.", &found), std::wstring(L"Hello there. Hello there."));
    CHECK(!found);
    CHECK_EQ(Collapse(L"I don't know, I don't know.", &found), std::wstring(L"I don't know, I don't know."));      /* a short phrase twice */
    CHECK(!found);
    CHECK_EQ(Collapse(L"We need to leave. We need to go.", &found), std::wstring(L"We need to leave. We need to go."));    /* alike, not equal */
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

TEST(AsrText_Utf8Encodes)
{
    CHECK_EQ(Utf8FromWide(L""), std::string());
    CHECK_EQ(Utf8FromWide(L"plain ascii"), std::string("plain ascii"));
    CHECK_EQ(Utf8FromWide(L"caf\u00e9"), std::string("caf\xC3\xA9"));                       /* two bytes */
    CHECK_EQ(Utf8FromWide(L"\u2019"), std::string("\xE2\x80\x99"));                        /* three bytes: a curly quote */
    CHECK_EQ(Utf8FromWide(L"\u65e5\u672c"), std::string("\xE6\x97\xA5\xE6\x9C\xAC"));      /* two CJK characters */
    CHECK_EQ(Utf8FromWide(L"\U0001F600"), std::string("\xF0\x9F\x98\x80"));              /* a surrogate pair: four bytes */
}

TEST(AsrText_Utf8EncodingRoundTripsAndReplacesALoneSurrogate)
{
    const std::wstring text = L"\u041f\u0440\u0438\u0432\u0435\u0442, \u4f60\u597d, \u039a\u03b1\u03bb\u03b7\u03bc\u03ad\u03c1\u03b1, caf\u00e9, \U0001F600!";
    CHECK_EQ(WideFromUtf8(Utf8FromWide(text)), text);

    const std::string replacement = "\xEF\xBF\xBD";                                      /* U+FFFD */
    CHECK_EQ(Utf8FromWide(std::wstring(1, static_cast<wchar_t>(0xD83D))), replacement);   /* a high half alone */
    CHECK_EQ(Utf8FromWide(std::wstring(1, static_cast<wchar_t>(0xDE00))), replacement);   /* a low half alone */
    CHECK_EQ(Utf8FromWide(std::wstring(L"a") + static_cast<wchar_t>(0xD83D) + L"b"),
             std::string("a") + replacement + "b");                                         /* and what is round it stays */
}
