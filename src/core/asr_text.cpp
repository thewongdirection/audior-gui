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

bool In(wchar_t c, unsigned first, unsigned last) { return c >= first && c <= last; }

/* Something that can be said: a letter or a digit.  Above ASCII, the letters of every script count; the
 * punctuation of Latin-1, of general use and of the CJK and full-width blocks (and of Arabic and Hindi),
 * symbols and notes, emoji (surrogates) and the replacement character do not. */
bool IsSpoken(wchar_t c)
{
    if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'))
        return true;
    if (c < 0xC0)
        return c == 0xAA || c == 0xB5 || c == 0xBA;                 /* the few letters among Latin-1's symbols */
    if (c == 0xD7 || c == 0xF7 || c == 0xFFFD || In(c, 0xD800, 0xDFFF) || In(c, 0x2000, 0x2BFF) || In(c, 0x2E00, 0x2E7F))
        return false;
    if (c == 0x60C || c == 0x61B || c == 0x61F || In(c, 0x66A, 0x66D) || c == 0x6D4 || In(c, 0x964, 0x965))
        return false;                                               /* Arabic and Devanagari punctuation */
    if (In(c, 0x3000, 0x3004) || In(c, 0x3008, 0x3020) || c == 0x3030 || In(c, 0x303D, 0x303F) || c == 0x30A0 || c == 0x30FB)
        return false;                                               /* CJK punctuation, brackets, the middle dot */
    if (In(c, 0xFF01, 0xFF0F) || In(c, 0xFF1A, 0xFF20) || In(c, 0xFF3B, 0xFF40) || In(c, 0xFF5B, 0xFF65) || In(c, 0xFFE0, 0xFFEE))
        return false;                                               /* the same, full width */
    return true;
}

bool IsHan(wchar_t c) { return In(c, 0x3400, 0x4DBF) || In(c, 0x4E00, 0x9FFF) || In(c, 0xF900, 0xFAFF); }

/* A character of a script written without spaces between words (Chinese, Japanese, Thai, Lao, Burmese, Khmer): to
 * the loop guard each one is a word.  A pair of surrogates for one of the rarer Chinese characters is one. */
bool IsUnspaced(const std::wstring &text, std::size_t i)
{
    const wchar_t c = text[i];
    if (In(c, 0x3041, 0x30FF))                          /* kana, but not the double hyphen and the middle dot */
        return c != 0x30A0 && c != 0x30FB;
    return IsHan(c) || In(c, 0x0E00, 0x0EFF) || In(c, 0x1000, 0x109F) || In(c, 0x1780, 0x17FF) ||
           (In(c, 0xD840, 0xD87F) && i + 1 < text.size() && In(text[i + 1], 0xDC00, 0xDFFF));
}

/* The full-width mark Chinese text has in place of an ASCII one, or the mark itself: Whisper writes the commas and
 * question marks of Chinese as it does in English, whatever it was asked.  A full stop only counts at the end of a
 * sentence ("3.5" is a number). */
wchar_t FullWidthMark(wchar_t mark, bool endsSentence)
{
    switch (mark) {
    case L',': return 0xFF0C;
    case L'?': return 0xFF1F;
    case L'!': return 0xFF01;
    case L':': return 0xFF1A;
    case L';': return 0xFF1B;
    case L'.': return endsSentence ? 0x3002 : mark;
    default: return mark;
    }
}

/* What ends an annotation that `open` begins, or 0 if it begins none: [..], (..), *..*, and the full-width （..）, 【..】, ［..］. */
wchar_t ClosingMark(wchar_t open)
{
    switch (open) {
    case L'[': return L']';
    case L'(': return L')';
    case L'*': return L'*';
    case 0xFF08: return 0xFF09;
    case 0x3010: return 0x3011;
    case 0xFF3B: return 0xFF3D;
    default: return 0;
    }
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

struct Word {
    std::wstring text;
    bool         spaceBefore;               /* white space came before it: it is put back when the word is kept */
};

/* The words of a text: what is between spaces, except that each character of an unspaced script is one on its
 * own (with the punctuation after it), and a Latin word ends where such a character begins. */
std::vector<Word> Words(const std::wstring &text)
{
    std::vector<Word> words;
    bool space = false;
    for (std::size_t at = 0; at < text.size();) {
        if (IsSpace(text[at])) {
            space = true;
            ++at;
            continue;
        }
        const std::size_t from = at;
        if (IsUnspaced(text, at)) {
            at += In(text[at], 0xD800, 0xDBFF) ? 2 : 1;
            while (at < text.size() && !IsSpace(text[at]) && !IsSpoken(text[at]) && !IsUnspaced(text, at))
                ++at;                               /* "好，" and "你好。": the mark goes with the character */
        } else {
            while (at < text.size() && !IsSpace(text[at]) && !IsUnspaced(text, at))
                ++at;
        }
        words.push_back({text.substr(from, at - from), space});
        space = false;
    }
    return words;
}

/* A word as compared: lower case, without punctuation at its ends. */
std::wstring Plain(const std::wstring &word)
{
    std::size_t from = 0, to = word.size();
    while (from < to && !IsSpoken(word[from]) && !IsUnspaced(word, from))      /* (a pair of surrogates may be a character) */
        ++from;
    while (to > from && !IsSpoken(word[to - 1]) &&
           !(to - 1 > from && In(word[to - 1], 0xDC00, 0xDFFF) && IsUnspaced(word, to - 2)))
        --to;
    std::wstring plain = word.substr(from, to - from);
    for (wchar_t &c : plain)
        c = c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    return plain;
}

}  // namespace

std::wstring RemoveRepeatedPhrases(const std::wstring &text, bool *found)
{
    const std::vector<Word> words = Words(text);
    std::vector<std::wstring> plain;
    std::size_t unspaced = 0;
    for (const Word &w : words) {
        plain.push_back(Plain(w.text));
        unspaced += IsUnspaced(w.text, 0);
    }
    /* A character of such a script is about half a word: a phrase has to be twice as long in them to count as long. */
    const std::size_t scale = unspaced * 2 > words.size() ? 2 : 1;

    /* Words that are nothing but marks ("...", a run of notes) have nothing to compare: they are never the same. */
    const auto same = [](const std::wstring &a, const std::wstring &b) { return !a.empty() && a == b; };

    *found = false;
    std::wstring out;
    for (std::size_t i = 0; i < words.size();) {
        std::size_t skip = 0;                           /* how many words of a loop starting here can be dropped */
        std::size_t kept = 1;
        bool loop = false;                              /* ...and whether that was a loop, not just a phrase said twice */
        for (std::size_t length = 1; length <= kMaxPhraseWords * scale && i + length < words.size(); ++length) {
            const std::size_t inWords = (length + scale - 1) / scale;
            std::size_t repeats = 1;
            while (i + (repeats + 1) * length <= words.size() &&
                   std::equal(plain.begin() + i, plain.begin() + i + length, plain.begin() + i + repeats * length, same))
                ++repeats;
            /* A last copy that was cut short (the model ran out of room): the words after the phrase begin it again. */
            std::size_t tail = 0;
            const std::size_t after = i + repeats * length;
            while (tail + 1 < length && after + tail < words.size() && same(plain[i + tail], plain[after + tail]))
                ++tail;
            const bool cutShort = after + tail == words.size();         /* the cut-off copy ends the text */
            const std::size_t extra = inWords >= kPartialCopyMinWords && tail >= kLongPhraseWords * scale && cutShort ? tail : 0;
            /* one word, which people do say twice or thrice, has to fill as much as six of them (six characters, or three
             * of two, are not a loop in Chinese: it takes twelve of one and six of two) */
            const std::size_t loopRepeats = inWords == 1 ? (kLoopRepeatsOfOneWord * scale + length - 1) / length : kLoopRepeats;
            const std::size_t needed = inWords >= kLongPhraseWords ? kLongPhraseRepeats : loopRepeats;
            if ((repeats >= needed || extra > 0) && (repeats - 1) * length + extra > skip) {
                skip = (repeats - 1) * length + extra;
                kept = length;
                loop = repeats >= loopRepeats;
            }
        }
        for (std::size_t k = 0; k < kept; ++k) {
            if (!out.empty() && words[i + k].spaceBefore)
                out.push_back(L' ');
            out += words[i + k].text;
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
        const wchar_t close = ClosingMark(open);
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
    for (std::size_t i = 0; i < kept.size(); ++i) {
        const wchar_t c = kept[i];
        if (IsSpace(c)) {
            if (!out.empty() && out.back() != L' ')
                out.push_back(L' ');
        } else if (c != 0xFFFD) {                       /* a model cut in the middle of a character leaves this behind */
            const bool afterHan = !out.empty() && IsHan(out.back());
            out.push_back(afterHan ? FullWidthMark(c, i + 1 == kept.size() || IsSpace(kept[i + 1])) : c);
            spoken = spoken || IsSpoken(c);
        }
    }
    while (!out.empty() && out.back() == L' ')
        out.pop_back();
    return spoken ? out : std::wstring();
}

}  // namespace audiorgui
