#include "line_editor.h"

#include <algorithm>

namespace audiorgui {

namespace {

bool IsHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

}  // namespace

bool LineEditor::SetText(const std::wstring &text)
{
    const std::wstring before = text_;
    text_.clear();
    caret_ = anchor_ = 0;
    Insert(text);
    return text_ != before;
}

bool LineEditor::Insert(const std::wstring &text)
{
    std::wstring accepted;
    for (wchar_t c : text) {
        if (!accepts_ || accepts_(c))
            accepted += c;
    }
    const std::size_t kept = text_.size() - (SelectionEnd() - SelectionStart());
    const std::size_t room = maxLength_ > kept ? maxLength_ - kept : 0;
    const bool cut = accepted.size() > room;
    accepted.resize(std::min(accepted.size(), room));
    if (cut && !accepted.empty() && IsHighSurrogate(accepted.back()))
        accepted.pop_back();                 /* do not keep half of a character */
    if (accepted.empty())
        return false;                        /* nothing typed that may be typed: leave any selection alone */

    text_.replace(SelectionStart(), SelectionEnd() - SelectionStart(), accepted);
    caret_ = anchor_ = SelectionStart() + accepted.size();
    return true;
}

bool LineEditor::DeleteSelection()
{
    if (!HasSelection())
        return false;
    text_.erase(SelectionStart(), SelectionEnd() - SelectionStart());
    caret_ = anchor_ = std::min(caret_, anchor_);
    return true;
}

bool LineEditor::Backspace(bool wholeWord)
{
    if (!HasSelection())
        anchor_ = wholeWord ? WordBefore(caret_) : CharBefore(caret_);
    return DeleteSelection();
}

bool LineEditor::Delete(bool wholeWord)
{
    if (!HasSelection())
        anchor_ = wholeWord ? WordAfter(caret_) : CharAfter(caret_);
    return DeleteSelection();
}

void LineEditor::Move(std::size_t position, bool extend)
{
    caret_ = std::min(position, text_.size());
    if (!extend)
        anchor_ = caret_;
}

void LineEditor::Left(bool extend, bool wholeWord)
{
    if (!extend && HasSelection())
        Move(SelectionStart(), false);
    else
        Move(wholeWord ? WordBefore(caret_) : CharBefore(caret_), extend);
}

void LineEditor::Right(bool extend, bool wholeWord)
{
    if (!extend && HasSelection())
        Move(SelectionEnd(), false);
    else
        Move(wholeWord ? WordAfter(caret_) : CharAfter(caret_), extend);
}

void LineEditor::SelectAll()
{
    anchor_ = 0;
    caret_ = text_.size();
}

std::size_t LineEditor::CharBefore(std::size_t from) const
{
    if (from == 0)
        return 0;
    --from;
    if (from > 0 && IsLowSurrogate(text_[from]) && IsHighSurrogate(text_[from - 1]))
        --from;
    return from;
}

std::size_t LineEditor::CharAfter(std::size_t from) const
{
    if (from >= text_.size())
        return text_.size();
    ++from;
    if (from < text_.size() && IsHighSurrogate(text_[from - 1]) && IsLowSurrogate(text_[from]))
        ++from;
    return from;
}

/* Back over spaces, then over the word before them. */
std::size_t LineEditor::WordBefore(std::size_t from) const
{
    std::size_t i = from;
    while (i > 0 && text_[i - 1] == L' ')
        --i;
    while (i > 0 && text_[i - 1] != L' ')
        --i;
    return i;
}

/* Forward over the rest of the word, then over the spaces after it. */
std::size_t LineEditor::WordAfter(std::size_t from) const
{
    std::size_t i = from;
    while (i < text_.size() && text_[i] != L' ')
        ++i;
    while (i < text_.size() && text_[i] == L' ')
        ++i;
    return i;
}

}  // namespace audiorgui
