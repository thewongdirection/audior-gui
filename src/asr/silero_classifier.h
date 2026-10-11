/*
 * silero_classifier.h - Voice-activity detection with the Silero model, through
 * whisper.cpp's own support for it.
 *
 * Silero looks at 32 ms of audio (512 samples) at a time and says how likely it
 * is to be speech, remembering what came before.  That tells speech from a fan,
 * music or a keyboard, which loudness alone cannot.  A frame counts as speech
 * from a probability of 0.5 on, and keeps counting down to 0.35, so a word does
 * not flicker at the threshold.  One classifier per stream: it has a memory.
 */
#pragma once

#include <memory>
#include <string>

#include "../core/utterance_tracker.h"

namespace audiorasr {

/* The tracker settings that suit Silero's frame. */
audiorgui::TrackerConfig SileroTrackerConfig();

/* Loads the model (a path in UTF-8); null if it cannot be loaded. */
std::unique_ptr<audiorgui::FrameClassifier> MakeSileroClassifier(const std::string &modelPath);

}  // namespace audiorasr
