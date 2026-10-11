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

/* A speech model sometimes gets stuck and repeats a phrase until it runs out
 * of room.  Keeps one copy of any run of a phrase (up to 12 words, compared
 * without case or punctuation) that repeats three times or more in a row (six
 * for a single word, which people do say twice or thrice); *found tells whether
 * anything was removed. */
std::wstring RemoveRepeatedPhrases(const std::wstring &text, bool *found);

/* Removes bracketed annotations ("[...]", "(...)" and "*...*", up to 40
 * characters each and with a letter in them: "(1)" stays), collapses white
 * space to single spaces and trims the ends.
 * If nothing but punctuation and symbols is left, the result is empty. */
std::wstring CleanTranscript(const std::wstring &text);

}  // namespace audiorgui
