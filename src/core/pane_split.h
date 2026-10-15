/*
 * pane_split.h - How the window's room for the stream panes is shared out.
 *
 * Two panes, the microphone's above the system audio's, take the room, each as tall as the other.  With the
 * two streams merged into one file the one transcript has a card of its own between them: only a bar while
 * its transcript is off, and while it is on the card takes what the panes can spare, but the panes keep a
 * height that shows their controls and a waveform, if the room allows.  Plain arithmetic, so that it is
 * unit-tested; the window only lays its rectangles out by the numbers.
 */
#pragma once

namespace audiorgui {

/* The card between the panes. */
enum class MiddleCard { None, Bar, Open };

/* The height of each of the two panes, and of the card between them (0 for none).  The gaps between the
 * panes and the card are not counted in these: pane + pane + middle + the gaps are `room`. */
struct PaneSplit {
    float pane   = 0.0f;
    float middle = 0.0f;
};

/* `room`: the height to share; `gap`: the space between neighbours; `bar`: the height of a bar.  An open
 * card is never shorter than the bar, and a pane is never negative, however small the room. */
PaneSplit SplitPanes(float room, float gap, MiddleCard card, float bar);

}  // namespace audiorgui
