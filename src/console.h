/*
 * console.h - Text output for a program built for the GUI subsystem.
 *
 * audior_gui has no console of its own, but --help, --list-devices and error
 * messages should still appear where it was started from: the console of the
 * command prompt that launched it, or the file or pipe its output was
 * redirected to.  If it has neither (a double-click, a shortcut), the text goes
 * into a message box instead.
 */
#pragma once

#include <string>

namespace audiorgui {

void WriteOut(const std::wstring &text);        /* standard output; "\n" ends a line */
void WriteError(const std::wstring &text);      /* standard error                     */

}  // namespace audiorgui
