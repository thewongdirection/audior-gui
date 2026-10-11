/*
 * asr_text.h - Text from the speech engine, made fit to show.
 *
 * Engines answer in UTF-8, and a model that has heard silence or music writes
 * annotations of its own ("[BLANK_AUDIO]", "(music)", "*sighs*", notes).  Those
 * are not speech: they are removed, so a pane never shows them.  Plain C++.
 */
#pragma once

#include <string>

namespace audiorgui {

/* UTF-8 to UTF-16 (a byte sequence that is not valid UTF-8 becomes U+FFFD). */
std::wstring WideFromUtf8(const std::string &utf8);

/* UTF-16 to UTF-8 (a surrogate without its partner becomes U+FFFD). */
std::string Utf8FromWide(const std::wstring &text);

/* A speech model sometimes gets stuck and repeats a phrase until it runs out
 * of room.  Keeps one copy of any run of a phrase (up to 30 words, compared
 * without case or punctuation) that repeats three times or more in a row (six
 * for a single word, which people do say twice or thrice); *found tells whether
 * such a loop was removed.  A phrase of five words or more that merely comes twice
 * in a row is cut to one copy too, but is not called a loop, and neither is a
 * last copy of a long phrase that was cut off at the end of the text. */
std::wstring RemoveRepeatedPhrases(const std::wstring &text, bool *found);

/* Removes bracketed annotations ("[...]", "(...)" and "*...*", up to 40
 * characters each and with a letter in them: "(1)" stays), collapses white
 * space to single spaces and trims the ends.
 * If nothing but punctuation and symbols is left, the result is empty. */
std::wstring CleanTranscript(const std::wstring &text);

}  // namespace audiorgui
