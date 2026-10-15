/*
 * theme.h - The design tokens: colours, and the sizes the layout is built on.
 *
 * A dark, low-contrast surface scheme with one accent colour per stream, so a
 * lane's waveform, its title dot and its meter all read as one thing.
 */
#pragma once

#include <windows.h>

#include <d2d1.h>

namespace audiorgui::ui {

constexpr D2D1_COLOR_F Rgb(unsigned hex, float alpha = 1.0f)
{
    return D2D1_COLOR_F{static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
                        static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
                        static_cast<float>(hex & 0xFF) / 255.0f, alpha};
}

constexpr D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float alpha)
{
    return D2D1_COLOR_F{c.r, c.g, c.b, alpha};
}

namespace theme {

/* Surfaces, darkest first. */
constexpr D2D1_COLOR_F kBackground = Rgb(0x0E1014);
constexpr D2D1_COLOR_F kSurface    = Rgb(0x161A21);
constexpr D2D1_COLOR_F kRaised     = Rgb(0x1E232D);
constexpr D2D1_COLOR_F kHover      = Rgb(0x262C38);
constexpr D2D1_COLOR_F kBorder     = Rgb(0x2A303C);

/* Text. */
constexpr D2D1_COLOR_F kText       = Rgb(0xE8EBF1);
constexpr D2D1_COLOR_F kTextMuted  = Rgb(0x929CAD);
constexpr D2D1_COLOR_F kTextFaint  = Rgb(0x5E6879);

/* Accents. */
constexpr D2D1_COLOR_F kMic        = Rgb(0x38BDF8);     /* microphone stream */
constexpr D2D1_COLOR_F kSystem     = Rgb(0xA78BFA);     /* system-audio stream */
constexpr D2D1_COLOR_F kMix        = Rgb(0x70A4F9);     /* the two streams merged: between those two */
constexpr D2D1_COLOR_F kRecord     = Rgb(0xF43F5E);
constexpr D2D1_COLOR_F kRecordHot  = Rgb(0xFB5D77);
constexpr D2D1_COLOR_F kOk         = Rgb(0x34D399);
constexpr D2D1_COLOR_F kWarn       = Rgb(0xFBBF24);
constexpr D2D1_COLOR_F kError      = Rgb(0xF87171);
constexpr D2D1_COLOR_F kFocusRing  = Rgb(0x7DD3FC);

/* Metrics (DIPs). */
constexpr float kMargin     = 24.0f;
constexpr float kGap        = 16.0f;
constexpr float kHeaderH    = 64.0f;
constexpr float kTransportH = 96.0f;
constexpr float kPanelW     = 340.0f;
constexpr float kCardRadius = 16.0f;
constexpr float kControlRadius = 10.0f;

}  // namespace theme
}  // namespace audiorgui::ui
