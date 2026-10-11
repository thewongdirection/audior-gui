/*
 * asr_languages.h - The languages speech can be taken for.
 *
 * Whisper finds the language of each utterance by itself ("Auto") but now and then guesses wrong on a short
 * phrase, and it writes Chinese in Simplified or Traditional characters as it pleases (a mix of both, from one
 * phrase to the next, is common).  So the person can name the language, and a language that has more than one
 * script names the script too.  Plain C++, no Windows.
 */
#pragma once

#include <cstddef>
#include <string>

#include "types.h"

namespace audiorgui {

struct SpeechLanguageInfo {
    const wchar_t *word;            /* in the settings file and after --transcribe-language                 */
    const wchar_t *label;           /* for the user                                                         */
    const char    *code;            /* Whisper's code for it; "auto" lets the model find the language       */
    const char    *prompt;          /* UTF-8: a sentence that starts the text in the script wanted, or null */
};

/* The first two are SpeechLanguage::Auto and SpeechLanguage::English; a SpeechLanguage is an index into this. */
inline constexpr SpeechLanguageInfo kSpeechLanguages[] = {
    {L"auto", L"Auto", "auto", nullptr},
    {L"en", L"English", "en", nullptr},
    {L"zh", L"Chinese", "zh", "以下是普通话的句子。"},
    {L"zh-hant", L"Traditional Chinese", "zh", "以下是普通話的句子。"},
    {L"es", L"Spanish", "es", nullptr},
    {L"fr", L"French", "fr", nullptr},
    {L"de", L"German", "de", nullptr},
    {L"ja", L"Japanese", "ja", nullptr},
    {L"ko", L"Korean", "ko", nullptr},
    {L"pt", L"Portuguese", "pt", nullptr},
    {L"ru", L"Russian", "ru", nullptr},
    {L"it", L"Italian", "it", nullptr},
    {L"nl", L"Dutch", "nl", nullptr},
    {L"ar", L"Arabic", "ar", nullptr},
    {L"hi", L"Hindi", "hi", nullptr},
    {L"tr", L"Turkish", "tr", nullptr},
    {L"vi", L"Vietnamese", "vi", nullptr},
    {L"th", L"Thai", "th", nullptr},
    {L"pl", L"Polish", "pl", nullptr},
    {L"uk", L"Ukrainian", "uk", nullptr},
    {L"id", L"Indonesian", "id", nullptr},
};
inline constexpr std::size_t kSpeechLanguageCount = sizeof(kSpeechLanguages) / sizeof(kSpeechLanguages[0]);

inline bool ValidSpeechLanguage(std::size_t index) { return index < kSpeechLanguageCount; }

inline const SpeechLanguageInfo &InfoOf(SpeechLanguage language)
{
    return kSpeechLanguages[ValidSpeechLanguage(static_cast<std::size_t>(language)) ? static_cast<std::size_t>(language) : 0];
}

/* The language whose `word` this is (lower case), or false. */
inline bool FindSpeechLanguage(const std::wstring &word, SpeechLanguage *language)
{
    for (std::size_t i = 0; i < kSpeechLanguageCount; ++i) {
        if (word == kSpeechLanguages[i].word) {
            *language = static_cast<SpeechLanguage>(i);
            return true;
        }
    }
    return false;
}

/* The words of all of them, for a message: "auto, en, zh, ...". */
inline std::wstring SpeechLanguageWords()
{
    std::wstring words;
    for (std::size_t i = 0; i < kSpeechLanguageCount; ++i)
        words += (i ? L", " : L"") + std::wstring(kSpeechLanguages[i].word);
    return words;
}

}  // namespace audiorgui
