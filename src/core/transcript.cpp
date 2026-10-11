#include "transcript.h"

#include <cwchar>

namespace audiorgui {

void TranscriptEvent::SetText(const wchar_t *source, std::size_t length)
{
    if (length < kTranscriptChars) {
        std::wmemcpy(text, source, length);
        text[length] = L'\0';
        return;
    }
    const std::size_t room = kTranscriptChars - 2;      /* the ellipsis and the terminator */
    std::size_t cut = room;
    while (cut > 0 && source[cut] != L' ')              /* the last word boundary that fits */
        --cut;
    if (cut == 0)
        cut = room;                                     /* one huge word: cut it */
    std::wmemcpy(text, source, cut);
    text[cut] = L'…';
    text[cut + 1] = L'\0';
}

}  // namespace audiorgui
