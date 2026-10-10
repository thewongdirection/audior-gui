/*
 * line_editor.h - The text, caret and selection of a single-line text box.
 *
 * Plain C++ with no window or drawing code, so every editing rule is
 * unit-tested.  The text box widget feeds it keystrokes and mouse positions
 * and paints what it holds.  Positions are indices into the UTF-16 text; a
 * surrogate pair is never split.
 */
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace audiorgui {

class LineEditor {
public:
    /* Characters for which this returns false are dropped, typed or pasted. */
    using Accepts = std::function<bool(wchar_t)>;

    explicit LineEditor(std::size_t maxLength = 255, Accepts accepts = nullptr)
        : maxLength_(maxLength), accepts_(std::move(accepts)) {}

    const std::wstring &Text() const { return text_; }
    std::size_t Caret() const { return caret_; }
    bool        HasSelection() const { return caret_ != anchor_; }
    std::size_t SelectionStart() const { return caret_ < anchor_ ? caret_ : anchor_; }
    std::size_t SelectionEnd() const { return caret_ < anchor_ ? anchor_ : caret_; }
    std::wstring Selected() const { return text_.substr(SelectionStart(), SelectionEnd() - SelectionStart()); }

    /* The edits return true when the text changed. */

    /* Replaces everything; the caret goes to the end. */
    bool SetText(const std::wstring &text);

    /* Replaces the selection (or inserts at the caret) with `text`, less the
     * characters that are not accepted and whatever does not fit. */
    bool Insert(const std::wstring &text);

    bool Backspace(bool wholeWord);          /* the selection, else the character (or word) before the caret */
    bool Delete(bool wholeWord);             /* the selection, else the character (or word) after it         */
    bool DeleteSelection();

    /* Caret movement.  With `extend` the selection grows from where it began. */
    void Move(std::size_t position, bool extend);
    void Left(bool extend, bool wholeWord);
    void Right(bool extend, bool wholeWord);
    void Home(bool extend) { Move(0, extend); }
    void End(bool extend) { Move(text_.size(), extend); }
    void SelectAll();

private:
    std::size_t WordBefore(std::size_t from) const;
    std::size_t WordAfter(std::size_t from) const;
    std::size_t CharBefore(std::size_t from) const;      /* a character outside the BMP is two units: */
    std::size_t CharAfter(std::size_t from) const;       /* the caret and deletes never split one     */

    std::size_t  maxLength_;
    Accepts      accepts_;
    std::wstring text_;
    std::size_t  caret_  = 0;
    std::size_t  anchor_ = 0;                /* the other end of the selection */
};

}  // namespace audiorgui
