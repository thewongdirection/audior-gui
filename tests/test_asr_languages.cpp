/* Tests of the languages a transcript can be told to expect (core/asr_languages.h): the table is an index on the wire, so
 * what is where has to stay put, and every entry has to survive the settings file. */
#include <cstring>
#include <set>
#include <string>

#include "../src/core/asr_languages.h"
#include "../src/core/saved_settings.h"
#include "test_support.h"

using namespace audiorgui;

TEST(Languages_TheFirstTwoAreAutoAndEnglishBecauseTheEnumNamesThem)
{
    CHECK_EQ(static_cast<std::size_t>(SpeechLanguage::Auto), std::size_t(0));
    CHECK_EQ(static_cast<std::size_t>(SpeechLanguage::English), std::size_t(1));
    CHECK_EQ(std::wstring(InfoOf(SpeechLanguage::Auto).word), std::wstring(L"auto"));
    CHECK_EQ(std::wstring(InfoOf(SpeechLanguage::English).word), std::wstring(L"en"));
    CHECK_EQ(std::string(InfoOf(SpeechLanguage::Auto).code), std::string("auto"));       /* Whisper's own word for finding it itself */
    CHECK_EQ(std::string(InfoOf(SpeechLanguage::English).code), std::string("en"));
}

TEST(Languages_EveryEntryIsWellFormedAndWordsAreUniqueAndLowerCase)
{
    std::set<std::wstring> words;
    for (std::size_t i = 0; i < kSpeechLanguageCount; ++i) {
        const SpeechLanguageInfo &l = kSpeechLanguages[i];
        CHECK(l.word && *l.word && l.label && *l.label && l.code && *l.code);
        CHECK(words.insert(l.word).second);
        for (const wchar_t *c = l.word; *c; ++c)
            CHECK((*c >= L'a' && *c <= L'z') || *c == L'-');         /* what the settings file and the command line say */
        SpeechLanguage found = SpeechLanguage::Auto;
        CHECK(FindSpeechLanguage(l.word, &found) && static_cast<std::size_t>(found) == i);
        CHECK(ValidSpeechLanguage(i));
    }
    CHECK(!ValidSpeechLanguage(kSpeechLanguageCount));
    SpeechLanguage unchanged = SpeechLanguage::English;
    CHECK(!FindSpeechLanguage(L"klingon", &unchanged) && unchanged == SpeechLanguage::English);
}

TEST(Languages_OnlyChineseAsksForAScriptWithASampleSentence)
{
    int withPrompt = 0;
    for (std::size_t i = 0; i < kSpeechLanguageCount; ++i) {
        const SpeechLanguageInfo &l = kSpeechLanguages[i];
        if (!l.prompt)
            continue;
        ++withPrompt;
        CHECK_EQ(std::string(l.code), std::string("zh"));            /* the one language that has two scripts */
        CHECK(std::strlen(l.prompt) > 6);
    }
    CHECK_EQ(withPrompt, 2);                                         /* Simplified and Traditional */
    SpeechLanguage simplified = SpeechLanguage::Auto, traditional = SpeechLanguage::Auto;
    CHECK(FindSpeechLanguage(L"zh", &simplified) && FindSpeechLanguage(L"zh-hant", &traditional));
    CHECK(std::strcmp(InfoOf(simplified).prompt, InfoOf(traditional).prompt) != 0);
}

TEST(Languages_EveryLanguageSurvivesTheSettingsFile)
{
    for (std::size_t i = 0; i < kSpeechLanguageCount; ++i) {
        SavedSettings saved;
        saved.recording.transcribeLanguage = static_cast<SpeechLanguage>(i);
        SavedSettings back;
        ParseSavedSettings(FormatSavedSettings(saved), &back);
        CHECK_EQ(static_cast<std::size_t>(back.recording.transcribeLanguage), i);
    }
}
