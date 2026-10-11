/*
 * transcript.h - What an engine tells the GUI about a stream's live transcript.
 *
 * Speech is recognised utterance by utterance (a phrase between two pauses).
 * An engine reports each utterance as a series of partial events, every one
 * carrying the whole text so far (the last words may be revised), then once as
 * final, after which it never changes.  Events are plain structs with a fixed
 * text buffer, so pulling them allocates nothing, like peak bins.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace audiorgui {

/* The text of one utterance, terminator included.  Engines end an utterance
 * (at a pause, or after about 15 s of speech) well before it can fill this. */
constexpr std::size_t kTranscriptChars = 256;

struct TranscriptEvent {
    std::uint32_t utterance    = 0;         /* 0, 1, 2 ... for each recording                  */
    bool          isFinal      = false;
    double        startSeconds = 0.0;       /* recording time at which the utterance began     */
    wchar_t       text[kTranscriptChars] = {};

    /* Copies `length` characters.  Text that does not fit is cut at a word
     * boundary and ends in an ellipsis. */
    void SetText(const wchar_t *source, std::size_t length);
};

/* A speech model an engine can transcribe with. */
struct TranscriptModel {
    std::wstring file;                      /* what RecordingSettings::transcribeModel names */
    std::wstring label;                     /* for the user                                  */
    bool         multilingual = false;      /* false: English only                           */
};

/* What a stream's transcription is doing.  A problem here never ends the
 * recording; it only changes this stream's state. */
enum class TranscriptState { Off, Starting, Live, Failed };

struct TranscriptStatus {
    TranscriptState state      = TranscriptState::Off;
    float           lagSeconds = 0.0f;      /* Live: how far behind the speech the text runs */
};

}  // namespace audiorgui
