/*
 * system_setup.h - The start-up set-up of transcription, wired to this PC.
 *
 * TranscriptionSetup does the thinking with functions it is given; this gives it
 * the real ones: the hardware scan, the models folder and the download program.
 */
#pragma once

#include <memory>
#include <string>

#include "transcription_setup.h"

namespace audiorgui {

/* The set-up for the helper in `programDir` and the models folder `modelsDir`; it is not started.
 * `declined` is what the user answered last time to the offer to download a model (see
 * SavedSettings::downloadDeclined).  Null if the helper is not installed there: transcription is not
 * offered at all then, and there is nothing to set up. */
std::unique_ptr<TranscriptionSetup> MakeSystemSetup(const std::wstring &programDir, const std::wstring &modelsDir,
                                                    bool declined);

}  // namespace audiorgui
