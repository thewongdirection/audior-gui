/*
 * preferences.h - What the program remembers between runs.
 *
 * The settings of the last run (every recording setting and the two hotkeys,
 * see core/saved_settings.h) are kept in a text file, Audior-GUI.ini, in the
 * folder the program runs from, so a copy of the program carries its settings
 * with it.  The window writes it when a recording starts, when a hotkey is
 * changed and when the program ends, and main() reads it at start-up.
 *
 * Nothing here may get in the way of recording: a file that is missing,
 * unreadable, too big or garbled reads as the defaults, and a folder that
 * cannot be written to (Program Files, a read-only drive) only means that
 * nothing is remembered.  A file is never left half-written: it is written to
 * a temporary name beside it and moved over the old one.
 */
#pragma once

#include <string>
#include <utility>

#include "core/saved_settings.h"

namespace audiorgui {

class Preferences {
public:
    /* `path` is the file; the tests give it a folder of their own. */
    explicit Preferences(std::wstring path = DefaultPath()) : path_(std::move(path)) {}

    /* Audior-GUI.ini beside the program; empty if the program cannot tell where it is. */
    static std::wstring DefaultPath();

    /* Applies what the file says to *settings, which holds the defaults (see ParseSavedSettings). */
    void Load(SavedSettings *settings) const;

    /* False if the file could not be written; the old one is then left as it was. */
    bool Save(const SavedSettings &settings) const;

private:
    std::wstring path_;
};

}  // namespace audiorgui
