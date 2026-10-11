#include "stop_signal.h"

namespace audiorgui {

namespace {
const wchar_t *g_name = kStopEventName;
}

const wchar_t *StopEventName() { return g_name; }
void UseStopEventName(const wchar_t *name) { g_name = name; }

bool StopSignal::Create()
{
    Close();
    event_ = CreateEventW(nullptr, TRUE, FALSE, StopEventName());      /* manual reset */
    if (event_)
        ResetEvent(event_);
    return event_ != nullptr;
}

void StopSignal::Close()
{
    if (event_)
        CloseHandle(event_);
    event_ = nullptr;
}

bool StopSignal::Raised() const
{
    return event_ && WaitForSingleObject(event_, 0) == WAIT_OBJECT_0;
}

bool RequestStop()
{
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, StopEventName());
    if (!event)
        return false;
    SetEvent(event);
    CloseHandle(event);
    return true;
}

}  // namespace audiorgui
