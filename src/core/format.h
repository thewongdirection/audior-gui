/*
 * format.h - Text formatting for the numbers the window displays.
 */
#pragma once

#include <cstdint>
#include <string>

namespace audiorgui {

/* "00:12:34" (hours grow past two digits when they have to). */
std::wstring FormatDuration(double seconds);

/* "512 B", "1.4 KB", "12.3 MB", "1.2 GB" (binary units). */
std::wstring FormatBytes(std::uint64_t bytes);

/* Level of a linear amplitude (1.0 = full scale) in dBFS; -infinity for 0. */
float LinearToDb(float linear);

/* "-12 dB", or "-inf dB" (with the infinity sign) for silence. */
std::wstring FormatDb(float db);

}  // namespace audiorgui
