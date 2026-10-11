/*
 * transcript_file.h - One stream's live transcript, saved as a text file while the
 * recording runs.
 *
 * Each utterance becomes a line, "[0:06] The weather today ...", in a UTF-8 file with a
 * byte-order mark (so any language, and Notepad, read it right).  The file is made when
 * the first line comes, so a stream that is never spoken on leaves none, and it is
 * written line by line, so it is complete however long the recording is and nothing
 * grows in memory.  A problem (a full disk) is remembered and the lines after it are
 * dropped; it never ends the recording.
 *
 * It follows the events as the pane does: a settled utterance is written at once; what is
 * still being spoken is only remembered, and is written as it stood if the utterance ends
 * without settling (the helper died or was changed to a smaller model, or ran out of time
 * after Stop), so the file holds the words the pane shows.
 */
#pragma once

#include <windows.h>

#include <mutex>
#include <string>

#include "../core/transcript.h"

namespace audiorgui {

class TranscriptFile {
public:
    explicit TranscriptFile(std::wstring path) : path_(std::move(path)) {}
    ~TranscriptFile() { Close(); }
    TranscriptFile(const TranscriptFile &) = delete;
    TranscriptFile &operator=(const TranscriptFile &) = delete;

    /* The next event of the stream, in the order it came. */
    void Add(const TranscriptEvent &event);

    /* No more events: what is still being spoken is written, the file is complete, and anything
     * added later is ignored. */
    void Close();

    /* Where the file is (once a line is written; before that, where it would be: a name that is taken
     * gets " (2)" and so on, as a recording's does), and whether anything was written. */
    std::wstring Path() const;
    bool         Written() const;

    /* The Windows error that stopped the file, or 0. */
    DWORD Error() const;

private:
    bool Open();
    void WriteLine(double startSeconds, const wchar_t *text);          /* with the lock held */

    mutable std::mutex mutex_;
    std::wstring       path_;
    HANDLE             file_ = INVALID_HANDLE_VALUE;
    bool               written_ = false;
    bool               closed_ = false;
    DWORD              error_ = 0;

    TranscriptEvent    pending_;                        /* the utterance being spoken, as it stands */
    bool               hasPending_ = false;
    std::uint32_t      writtenAsItStood_ = 0;           /* the last utterance written without its final... */
    bool               hasWrittenAsItStood_ = false;    /* ...so that a final that still comes is not written twice */
};

}  // namespace audiorgui
