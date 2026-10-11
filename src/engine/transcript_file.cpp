#include "transcript_file.h"

#include <string>
#include <vector>

#include "../core/asr_text.h"
#include "../core/format.h"

namespace audiorgui {

namespace {

constexpr int kMaxNames = 99;                           /* " (2)" .. " (99)" before giving up on a free name */

std::wstring Numbered(const std::wstring &path, int n)
{
    const std::size_t dot = path.rfind(L'.');           /* the file always ends in ".txt" */
    return path.substr(0, dot) + L" (" + std::to_wstring(n) + L")" + path.substr(dot);
}

}  // namespace

/* Makes the file, and its folder if that is not there yet.  Never takes the place of a file that exists. */
bool TranscriptFile::Open()
{
    bool madeFolder = false;
    for (int n = 1; n <= kMaxNames; ++n) {
        const std::wstring candidate = n == 1 ? path_ : Numbered(path_, n);
        file_ = CreateFileW(candidate.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ != INVALID_HANDLE_VALUE) {
            path_ = candidate;
            return true;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_PATH_NOT_FOUND && !madeFolder) {
            madeFolder = true;
            const std::size_t slash = path_.find_last_of(L"\\/");
            if (slash != std::wstring::npos && slash > 0 && CreateDirectoryW(path_.substr(0, slash).c_str(), nullptr)) {
                --n;                                    /* the same name again, now that its folder exists */
                continue;
            }
        }
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            error_ = error;
            return false;
        }
    }
    error_ = ERROR_FILE_EXISTS;
    return false;
}

/* One utterance as a line; the first one makes the file.  The lock is held. */
void TranscriptFile::WriteLine(double startSeconds, const wchar_t *text)
{
    const std::wstring line = FormatTranscriptLine(startSeconds, text);
    if (line.empty() || error_ != 0)
        return;                                         /* nothing said, or it failed once: the rest is dropped, not retried */
    std::string bytes;
    if (file_ == INVALID_HANDLE_VALUE) {
        if (!Open())
            return;
        bytes = "\xEF\xBB\xBF";                         /* the UTF-8 byte-order mark, once */
    }
    bytes += Utf8FromWide(line + L"\r\n");

    DWORD wrote = 0;
    if (!WriteFile(file_, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr) || wrote != bytes.size()) {
        error_ = GetLastError() != 0 ? GetLastError() : ERROR_WRITE_FAULT;
        if (!written_) {                                /* the very first write failed: no empty file is left behind */
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
            DeleteFileW(path_.c_str());
        }
        return;
    }
    written_ = true;
}

void TranscriptFile::Add(const TranscriptEvent &event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_)
        return;
    if (hasPending_ && event.utterance > pending_.utterance) {      /* numbers only grow: the next utterance began */
        WriteLine(pending_.startSeconds, pending_.text);            /* so that one is over, as it stood */
        writtenAsItStood_ = pending_.utterance;
        hasWrittenAsItStood_ = true;
        hasPending_ = false;
    }
    if (hasWrittenAsItStood_ && event.utterance == writtenAsItStood_)
        return;                                         /* it is in the file already; nothing may follow it there */
    if (event.isFinal) {
        if (hasPending_ && pending_.utterance == event.utterance)
            hasPending_ = false;                        /* the final replaces its drafts, and an empty one takes the phrase back */
        WriteLine(event.startSeconds, event.text);
    } else if (!hasPending_ || pending_.utterance == event.utterance) {
        pending_ = event;                               /* (a stale draft of an older utterance is of no interest) */
        hasPending_ = true;
    }
}

void TranscriptFile::Close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_)
        return;
    closed_ = true;                                     /* an event that comes late must not make another file */
    if (hasPending_)
        WriteLine(pending_.startSeconds, pending_.text);        /* the words being spoken at the end */
    hasPending_ = false;
    if (file_ != INVALID_HANDLE_VALUE)
        CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
}

std::wstring TranscriptFile::Path() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return path_;
}

bool TranscriptFile::Written() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return written_;
}

DWORD TranscriptFile::Error() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

}  // namespace audiorgui
