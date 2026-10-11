#include "asr_text.h"

#include <algorithm>
#include <vector>

namespace audiorgui {

namespace {

constexpr std::size_t kMaxAnnotation = 40;

void Append(std::wstring *out, unsigned long codePoint)
{
    if (codePoint >= 0x10000 && sizeof(wchar_t) == 2) {             /* a surrogate pair */
        codePoint -= 0x10000;
        out->push_back(static_cast<wchar_t>(0xD800 + (codePoint >> 10)));
        out->push_back(static_cast<wchar_t>(0xDC00 + (codePoint & 0x3FF)));
    } else {
        out->push_back(static_cast<wchar_t>(codePoint));
    }
}

bool IsSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xA0; }

/* Something that can be said: a letter or a digit.  Above ASCII, the letters of every script count; the
 * Latin-1 punctuation, general punctuation, symbols and notes, emoji (surrogates) and the replacement
 * character do not. */
bool IsSpoken(wchar_t c)
{
    if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'))
        return true;
    if (c < 0xC0)
        return c == 0xAA || c == 0xB5 || c == 0xBA;                 /* the few letters among Latin-1's symbols */
    if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0xD800 && c <= 0xDFFF) || c == 0xFFFD)
        return false;
    return true;
}

/* Whether text[from, to) has a letter in it: "(laughs)" is an annotation, "(1)" and "5 * 3 * 2" are not. */
bool HasLetter(const std::wstring &text, std::size_t from, std::size_t to)
{
    for (std::size_t i = from; i < to; ++i) {
        if (IsSpoken(text[i]) && !(text[i] >= L'0' && text[i] <= L'9'))
            return true;
    }
    return false;
}

}  // namespace

std::wstring WideFromUtf8(const std::string &utf8)
{
    std::wstring out;
    out.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char lead = static_cast<unsigned char>(utf8[i]);
        unsigned long codePoint = 0xFFFD;
        std::size_t length = 1;
        if (lead < 0x80) {
            codePoint = lead;
        } else if (lead >= 0xC2 && lead <= 0xF4) {
            length = lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
            unsigned long value = lead & (0xFFu >> (length + 1));
            bool good = i + length <= utf8.size();
            for (std::size_t k = 1; good && k < length; ++k) {
                const unsigned char next = static_cast<unsigned char>(utf8[i + k]);
                good = (next & 0xC0) == 0x80;
                value = (value << 6) | (next & 0x3F);
            }
            const unsigned long smallest = length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
            if (good && value >= smallest && value <= 0x10FFFF && !(value >= 0xD800 && value <= 0xDFFF))
                codePoint = value;
            else
                length = 1;                             /* the bad lead byte alone is replaced */
        }
        Append(&out, codePoint);
        i += length;
    }
    return out;
}

std::string Utf8FromWide(const std::wstring &text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        unsigned long codePoint = static_cast<unsigned short>(text[i]);
        const bool high = codePoint >= 0xD800 && codePoint <= 0xDBFF;
        const unsigned long next = i + 1 < text.size() ? static_cast<unsigned short>(text[i + 1]) : 0;
        if (high && next >= 0xDC00 && next <= 0xDFFF) {
            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (next - 0xDC00);
            ++i;
        } else if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
            codePoint = 0xFFFD;                         /* a surrogate with no partner */
        }
        if (codePoint < 0x80) {
            out += static_cast<char>(codePoint);
        } else if (codePoint < 0x800) {
            out += static_cast<char>(0xC0 | (codePoint >> 6));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else if (codePoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codePoint >> 12));
            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codePoint >> 18));
            out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
    }
    return out;
}

namespace {

constexpr std::size_t kMaxPhraseWords = 30;             /* a long sentence, as the large model repeats whole ones */
constexpr std::size_t kLoopRepeats = 3, kLoopRepeatsOfOneWord = 6;
/* A phrase this long, said twice in a row word for word, is the model's doing, not the speaker's (the large
 * model does it when it is given a shortened audio window): one copy is kept, without calling it a loop.  So
 * is the start of a long phrase (at least 5 of its words) that follows a copy of it at once and ends the text:
 * the copy the model was cut off in. */
constexpr std::size_t kLongPhraseWords = 5, kLongPhraseRepeats = 2, kPartialCopyMinWords = 8;

std::vector<std::wstring> Words(const std::wstring &text)
{
    std::vector<std::wstring> words;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t end = std::min(text.find(L' ', at), text.size());
        if (end > at)
            words.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return words;
}

/* A word as compared: lower case, without punctuation at its ends. */
std::wstring Plain(const std::wstring &word)
{
    std::size_t from = 0, to = word.size();
    while (from < to && !IsSpoken(word[from]))
        ++from;
    while (to > from && !IsSpoken(word[to - 1]))
        --to;
    std::wstring plain = word.substr(from, to - from);
    for (wchar_t &c : plain)
        c = c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    return plain;
}

}  // namespace

std::wstring RemoveRepeatedPhrases(const std::wstring &text, bool *found)
{
    const std::vector<std::wstring> words = Words(text);
    std::vector<std::wstring> plain;
    for (const std::wstring &w : words)
        plain.push_back(Plain(w));

    *found = false;
    std::wstring out;
    for (std::size_t i = 0; i < words.size();) {
        std::size_t skip = 0;                           /* how many words of a loop starting here can be dropped */
        std::size_t kept = 1;
        bool loop = false;                              /* ...and whether that was a loop, not just a phrase said twice */
        for (std::size_t length = 1; length <= kMaxPhraseWords && i + length < words.size(); ++length) {
            std::size_t repeats = 1;
            while (i + (repeats + 1) * length <= words.size() &&
                   std::equal(plain.begin() + i, plain.begin() + i + length, plain.begin() + i + repeats * length))
                ++repeats;
            /* A last copy that was cut short (the model ran out of room): the words after the phrase begin it again. */
            std::size_t tail = 0;
            const std::size_t after = i + repeats * length;
            while (tail + 1 < length && after + tail < words.size() && plain[i + tail] == plain[after + tail])
                ++tail;
            const bool cutShort = after + tail == words.size();         /* the cut-off copy ends the text */
            const std::size_t extra = length >= kPartialCopyMinWords && tail >= kLongPhraseWords && cutShort ? tail : 0;
            const std::size_t loopRepeats = length == 1 ? kLoopRepeatsOfOneWord : kLoopRepeats;
            const std::size_t needed = length >= kLongPhraseWords ? kLongPhraseRepeats : loopRepeats;
            if ((repeats >= needed || extra > 0) && (repeats - 1) * length + extra > skip) {
                skip = (repeats - 1) * length + extra;
                kept = length;
                loop = repeats >= loopRepeats;
            }
        }
        for (std::size_t k = 0; k < kept; ++k) {
            if (!out.empty())
                out.push_back(L' ');
            out += words[i + k];
        }
        if (skip > 0 && loop)
            *found = true;
        i += kept + skip;
    }
    return out;
}

std::wstring CleanTranscript(const std::wstring &text)
{
    std::wstring kept;
    for (std::size_t i = 0; i < text.size();) {
        const wchar_t open = text[i];
        const wchar_t close = open == L'[' ? L']' : open == L'(' ? L')' : open == L'*' ? L'*' : 0;
        if (close) {
            const std::size_t end = text.find(close, i + 1);
            if (end != std::wstring::npos && end - i - 1 <= kMaxAnnotation && HasLetter(text, i + 1, end)) {
                kept.push_back(L' ');                   /* an annotation separates words like a space */
                i = end + 1;
                continue;
            }
        }
        kept.push_back(text[i++]);
    }

    std::wstring out;
    bool spoken = false;
    for (wchar_t c : kept) {
        if (IsSpace(c)) {
            if (!out.empty() && out.back() != L' ')
                out.push_back(L' ');
        } else {
            out.push_back(c);
            spoken = spoken || IsSpoken(c);
        }
    }
    while (!out.empty() && out.back() == L' ')
        out.pop_back();
    return spoken ? out : std::wstring();
}

}  // namespace audiorgui
