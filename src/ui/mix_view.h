/*
 * mix_view.h - Draws the card between the two panes when the streams are merged into one file.
 *
 * A merged recording has one live transcript, of the mix, so it has a card of its own between the
 * microphone's pane and the system audio's, and the panes have none:
 *
 *   +--------------------------------------------------------+
 *   |  (o) Transcript   [ Live transcription ]  Live  ...  Mix of both streams
 *   |  0:00  Okay, let's get started. Thanks everyone ...    |
 *   |  0:05  The agenda today is the Q4 launch timeline ...  |
 *   +--------------------------------------------------------+
 *
 * The switch is a widget placed by MainView in the header, as a pane's is.  With the switch off the
 * card is only the header, a bar; with it on the lines fill the rest, newest at the bottom (see
 * TranscriptView).
 */
#pragma once

#include "painter.h"
#include "transcript_view.h"

namespace audiorgui::ui {

class MixView {
public:
    /* The card's header: the title and the switch.  A card of this height is only that. */
    static constexpr float kBarHeight = 48.0f;

    /* `transcript.lane` is null while the switch is off. */
    void Paint(Painter &painter, const D2D1_RECT_F &card, const TranscriptPane &transcript, bool recording);

private:
    TranscriptView text_;
};

}  // namespace audiorgui::ui
