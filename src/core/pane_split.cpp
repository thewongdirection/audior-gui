#include "pane_split.h"

#include <algorithm>

namespace audiorgui {

namespace {

constexpr float kOpenPaneMin   = 150.0f;    /* a pane beside an open card: its controls and a strip of waveform...      */
constexpr float kOpenPaneMax   = 200.0f;    /* ...but no more than this, the card (the text) having the rest            */
constexpr float kOpenPaneShare = 0.3f;      /* and in between, this much of the room                                    */

}  // namespace

PaneSplit SplitPanes(float room, float gap, MiddleCard card, float bar)
{
    PaneSplit split;
    if (card == MiddleCard::None) {
        split.pane = std::max(0.0f, (room - gap) * 0.5f);
        return split;
    }

    const float spare = std::max(0.0f, (room - 2.0f * gap - bar) * 0.5f);      /* a pane's, with the card a bar */
    split.pane = card == MiddleCard::Open
                     ? std::min(spare, std::min(kOpenPaneMax, std::max(kOpenPaneMin, room * kOpenPaneShare)))
                     : spare;
    split.middle = std::max(0.0f, room - 2.0f * gap - 2.0f * split.pane);
    return split;
}

}  // namespace audiorgui
