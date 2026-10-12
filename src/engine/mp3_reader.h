/*
 * mp3_reader.h - Decodes an MP3 file to canonical PCM with Media Foundation's in-box decoder (see pcm_reader.h).
 *
 * Like the encoder behind Mp3Sink it is delay-loaded, so the program starts without Media Foundation (a Windows N
 * edition); opening an MP3 file then fails with a clear error.
 */
#pragma once

#include <memory>
#include <string>

#include "pcm_reader.h"

namespace audiorgui {

/* On failure returns null and says why in *hr. */
std::unique_ptr<IPcmReader> OpenMp3Reader(const std::wstring &path, HRESULT *hr);

}  // namespace audiorgui
