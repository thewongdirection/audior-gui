#include "../src/core/line_editor.h"
#include "../src/core/settings.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

LineEditor Editor(const std::wstring &text, std::size_t maxLength = 255)
{
    LineEditor e(maxLength, IsFileNameChar);
    e.SetText(text);
    return e;
}

}  // namespace

TEST(LineEditor_TypesAtTheCaret)
{
    LineEditor e = Editor(L"ac");
    e.Move(1, false);
    CHECK(e.Insert(L"b"));
    CHECK_EQ(e.Text(), std::wstring(L"abc"));
    CHECK_EQ(e.Caret(), std::size_t(2));
}

TEST(LineEditor_TypingReplacesTheSelection)
{
    LineEditor e = Editor(L"hello world");
    e.SelectAll();
    CHECK(e.Insert(L"x"));
    CHECK_EQ(e.Text(), std::wstring(L"x"));
    CHECK(!e.HasSelection());

    e.SetText(L"hello world");
    e.Move(0, false);
    e.Move(5, true);                                    /* "hello" */
    CHECK_EQ(e.Selected(), std::wstring(L"hello"));
    e.Insert(L"bye");
    CHECK_EQ(e.Text(), std::wstring(L"bye world"));
    CHECK_EQ(e.Caret(), std::size_t(3));
}

TEST(LineEditor_DropsCharactersThatAreNotAccepted)
{
    LineEditor e = Editor(L"");
    CHECK(e.Insert(L"a:b*c?d"));                        /* ':', '*' and '?' are not file name characters */
    CHECK_EQ(e.Text(), std::wstring(L"abcd"));
    CHECK(!e.Insert(L"\\/"));                           /* nothing acceptable: no change */
    CHECK(!e.Insert(L"\r\n\t"));
    CHECK_EQ(e.Text(), std::wstring(L"abcd"));
}

TEST(LineEditor_RejectedInputKeepsTheSelection)
{
    LineEditor e = Editor(L"hello");
    e.SelectAll();
    CHECK(!e.Insert(L"?"));
    CHECK_EQ(e.Text(), std::wstring(L"hello"));
    CHECK(e.HasSelection());
}

TEST(LineEditor_StopsAtTheMaximumLength)
{
    LineEditor e = Editor(L"", 5);
    CHECK(e.Insert(L"abcdefgh"));
    CHECK_EQ(e.Text(), std::wstring(L"abcde"));
    CHECK(!e.Insert(L"z"));                             /* full */
    e.Move(1, false);
    e.Move(3, true);                                    /* select "bc": room for two */
    CHECK(e.Insert(L"XYZ"));
    CHECK_EQ(e.Text(), std::wstring(L"aXYde"));
}

TEST(LineEditor_SetTextFiltersAndTruncates)
{
    LineEditor e(4, IsFileNameChar);
    CHECK(e.SetText(L"a|b|c|d|e"));
    CHECK_EQ(e.Text(), std::wstring(L"abcd"));
    CHECK_EQ(e.Caret(), std::size_t(4));
    CHECK(!e.SetText(L"abcd"));                         /* unchanged */
}

TEST(LineEditor_BackspaceAndDelete)
{
    LineEditor e = Editor(L"abcd");
    e.Move(2, false);
    CHECK(e.Backspace(false));
    CHECK_EQ(e.Text(), std::wstring(L"acd"));
    CHECK_EQ(e.Caret(), std::size_t(1));
    CHECK(e.Delete(false));
    CHECK_EQ(e.Text(), std::wstring(L"ad"));

    e.Home(false);
    CHECK(!e.Backspace(false));                         /* nothing before the caret */
    e.End(false);
    CHECK(!e.Delete(false));                            /* nothing after it */
}

TEST(LineEditor_DeletingASelectionRemovesJustThat)
{
    LineEditor e = Editor(L"abcdef");
    e.Move(1, false);
    e.Move(4, true);
    CHECK(e.Backspace(false));
    CHECK_EQ(e.Text(), std::wstring(L"aef"));
    CHECK_EQ(e.Caret(), std::size_t(1));

    e.SelectAll();
    CHECK(e.Delete(false));
    CHECK(e.Text().empty());
    CHECK(!e.DeleteSelection());
}

TEST(LineEditor_WholeWordEdits)
{
    LineEditor e = Editor(L"one two  three");
    e.End(false);
    CHECK(e.Backspace(true));
    CHECK_EQ(e.Text(), std::wstring(L"one two  "));
    CHECK(e.Backspace(true));
    CHECK_EQ(e.Text(), std::wstring(L"one "));

    e.Home(false);
    CHECK(e.Delete(true));
    CHECK(e.Text().empty());
}

TEST(LineEditor_ArrowsMoveAndCollapseTheSelection)
{
    LineEditor e = Editor(L"abc");
    e.Left(false, false);
    CHECK_EQ(e.Caret(), std::size_t(2));
    e.Left(true, false);                                /* extend */
    CHECK_EQ(e.Selected(), std::wstring(L"b"));
    e.Left(false, false);                               /* collapse to the start of the selection */
    CHECK(!e.HasSelection());
    CHECK_EQ(e.Caret(), std::size_t(1));

    e.Right(true, false);
    e.Right(false, false);                              /* collapse to its end */
    CHECK_EQ(e.Caret(), std::size_t(2));

    e.Home(false);
    e.Left(false, false);
    CHECK_EQ(e.Caret(), std::size_t(0));                /* stays put at the ends */
    e.End(false);
    e.Right(false, false);
    CHECK_EQ(e.Caret(), std::size_t(3));
}

TEST(LineEditor_ControlArrowsJumpWords)
{
    LineEditor e = Editor(L"one two three");
    e.Home(false);
    e.Right(false, true);
    CHECK_EQ(e.Caret(), std::size_t(4));                /* start of "two" */
    e.Right(false, true);
    CHECK_EQ(e.Caret(), std::size_t(8));
    e.Left(false, true);
    CHECK_EQ(e.Caret(), std::size_t(4));
    e.Home(true);
    CHECK_EQ(e.Selected(), std::wstring(L"one "));
}

TEST(LineEditor_MovingPastTheEndsClamps)
{
    LineEditor e = Editor(L"abc");
    e.Move(99, false);
    CHECK_EQ(e.Caret(), std::size_t(3));
    e.Move(1, true);
    CHECK_EQ(e.SelectionStart(), std::size_t(1));
    CHECK_EQ(e.SelectionEnd(), std::size_t(3));
}

TEST(LineEditor_NeverSplitsASurrogatePair)
{
    LineEditor e = Editor(L"a\U0001F600b");                  /* a, an emoji (two UTF-16 units), b */
    CHECK_EQ(e.Text().size(), std::size_t(4));
    e.Left(false, false);                                       /* before b */
    CHECK_EQ(e.Caret(), std::size_t(3));
    e.Left(false, false);                                       /* over the whole emoji */
    CHECK_EQ(e.Caret(), std::size_t(1));
    e.Right(false, false);
    CHECK_EQ(e.Caret(), std::size_t(3));

    CHECK(e.Backspace(false));                                  /* removes it whole */
    CHECK_EQ(e.Text(), std::wstring(L"ab"));

    e = Editor(L"a\U0001F600b");
    e.Move(1, false);
    CHECK(e.Delete(false));
    CHECK_EQ(e.Text(), std::wstring(L"ab"));
}

TEST(LineEditor_TruncationDoesNotKeepHalfACharacter)
{
    LineEditor e = Editor(L"", 2);
    CHECK(e.Insert(L"a\U0001F600"));                          /* the emoji would be cut after its first unit */
    CHECK_EQ(e.Text(), std::wstring(L"a"));

    e = Editor(L"", 4);
    CHECK(e.Insert(L"\xD83D"));                                /* typed one unit at a time, as WM_CHAR delivers it */
    CHECK(e.Insert(L"\xDE00"));
    CHECK_EQ(e.Text(), std::wstring(L"\U0001F600"));
}
