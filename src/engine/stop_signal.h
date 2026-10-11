/*
 * stop_signal.h - Lets a second process end the recording in progress.
 *
 * While a recording runs, its process holds a named event.  `Audior-GUI
 * --stop` (or audior's own `--stop`: the name is the same) opens that event and
 * sets it.  No event exists when nothing is recording, which is how "No active
 * recording to stop." is told apart.
 */
#pragma once

#include <windows.h>

namespace audiorgui {

/* The event's name; shared with audior so either program's --stop works. */
inline constexpr wchar_t kStopEventName[] = L"Local\\AudiorStopEvent";

/* The name in use: kStopEventName, except in the unit tests, which give themselves a name of their own
 * so that they neither see nor stop a recording that is really running.  `name` must stay valid; set it
 * before anything is recorded. */
const wchar_t *StopEventName();
void UseStopEventName(const wchar_t *name);

class StopSignal {
public:
    StopSignal() = default;
    ~StopSignal() { Close(); }
    StopSignal(const StopSignal &) = delete;
    StopSignal &operator=(const StopSignal &) = delete;

    /* Creates the event (or joins one that exists) and clears any stale
     * signal.  False if Windows refuses. */
    bool Create();
    void Close();

    /* Signalled once another process asked for the recording to stop. */
    HANDLE Handle() const { return event_; }
    bool   Raised() const;

private:
    HANDLE event_ = nullptr;
};

/* Asks the recording in progress (in any process) to stop.  False if there is
 * none. */
bool RequestStop();

}  // namespace audiorgui
