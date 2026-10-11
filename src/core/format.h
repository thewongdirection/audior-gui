/*
 * format.h - Text formatting for the numbers the window displays, and reading
 * back whole numbers typed or saved as text.
 */
#pragma once

#include <cstdint>
#include <string>

namespace audiorgui {

/* "00:12:34" (hours grow past two digits when they have to). */
std::wstring FormatDuration(double seconds);

/* "0:07", "12:34", "1:02:03": a moment in a recording, for the transcript. */
std::wstring FormatClock(double seconds);

/* A settled utterance as one line of a transcript file: "[0:06] The weather today ...", with no line break
 * (the clock is the one the panes show).  Empty for text that is only white space: a phrase taken back. */
std::wstring FormatTranscriptLine(double startSeconds, const wchar_t *text);

/* "0.4 s": how far behind the speech a transcript runs (one decimal). */
std::wstring FormatLag(float seconds);

/* "512 B", "1.4 KB", "12.3 MB", "1.2 GB" (binary units). */
std::wstring FormatBytes(std::uint64_t bytes);

/* Level of a linear amplitude (1.0 = full scale) in dBFS; -infinity for 0. */
float LinearToDb(float linear);

/* "-12 dB", or "-inf dB" (with the infinity sign) for silence. */
std::wstring FormatDb(float db);

/* A whole number written as ASCII digits only, at most nine so that it fits an int; false (leaving
 * *value alone) for anything else, a sign and the empty string included. */
bool ParseWholeNumber(const std::wstring &text, int *value);

}  // namespace audiorgui
