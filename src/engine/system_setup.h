/*
 * system_setup.h - The start-up set-up of transcription, wired to this PC.
 *
 * TranscriptionSetup does the thinking with functions it is given; this gives it
 * the real ones: the hardware scan, the models folder, the download program and
 * the registry value that remembers a "not now".
 */
#pragma once

#include <memory>
#include <string>

#include "../preferences.h"
#include "transcription_setup.h"

namespace audiorgui {

/* The set-up for the helper in `programDir` and the models folder `modelsDir`; it is not started.
 * Null if the helper is not installed there: transcription is not offered at all then, and there is
 * nothing to set up. */
std::unique_ptr<TranscriptionSetup> MakeSystemSetup(const std::wstring &programDir, const std::wstring &modelsDir,
                                                    const Preferences &preferences);

}  // namespace audiorgui
