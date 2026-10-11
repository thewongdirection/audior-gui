#include <set>
#include <string>

#include "../src/core/asr_models.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

const std::string kHashA(64, 'a'), kHashB(64, 'b');

bool Parses(const std::string &text, std::vector<ManifestEntry> *entries = nullptr)
{
    std::vector<ManifestEntry> local;
    std::wstring problem;
    const bool ok = ParseManifest(text, entries ? entries : &local, &problem);
    CHECK_EQ(ok, problem.empty());                      /* a reason if, and only if, it was refused */
    return ok;
}

}  // namespace

/* ---- the table: every model names where it comes from, its licence and its hash ---- */

TEST(Models_EveryEntryIsCompleteAndUnique)
{
    std::set<std::wstring> files;
    std::set<std::string> hashes;
    auto check = [&](const AsrModel &m) {
        const std::wstring file = m.file;
        CHECK(file.size() > 4 && file.compare(file.size() - 4, 4, L".bin") == 0);
        CHECK(file.find_first_of(L"\\/:") == std::wstring::npos);          /* a plain file name */

        const std::string hash = m.sha256;
        CHECK_EQ(hash.size(), std::size_t(64));
        for (char c : hash)
            CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));       /* lower-case hex */

        CHECK(m.bytes > 0);
        CHECK(m.label && *m.label);
        CHECK(m.licence && *m.licence);                                     /* no licence, no model */
        CHECK(m.source && std::wstring(m.source).compare(0, 8, L"https://") == 0);
        CHECK(files.insert(file).second);
        CHECK(hashes.insert(hash).second);
    };
    static_assert(kAsrModelCount > 0, "the recorder needs at least one model to offer");
    for (const AsrModel &model : kAsrModels)
        check(model);
    check(kVadModel);
}

TEST(Models_ADownloadComesFromTheFilesPageOnHuggingFace)
{
    CHECK_EQ(ModelUrl(kAsrModels[0]),
             std::wstring(L"https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin"));
    CHECK_EQ(ModelUrl(kVadModel),
             std::wstring(L"https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin"));
    for (const AsrModel &m : kAsrModels)                /* the download program takes only this host (src/fetch) */
        CHECK(ModelUrl(m).rfind(L"https://huggingface.co/", 0) == 0);
}

TEST(Models_AtLeastOneModelIsOfferedToEveryPc)
{
    bool forEveryPc = false;                            /* or a PC without a good card would be offered nothing */
    for (const AsrModel &m : kAsrModels)
        forEveryPc = forEveryPc || (m.gpuMemoryBytes == 0 && m.offered);
    CHECK(forEveryPc);
    CHECK_EQ(kVadModel.gpuMemoryBytes, std::uint64_t(0));
}

TEST(Models_NoneIsEnglishOnly)
{
    for (const AsrModel &m : kAsrModels)                /* whisper.cpp's own naming: ".en" is English only, and a person who speaks
                                                           another language gets nothing from such a model */
        CHECK(std::wstring(m.file).find(L".en.") == std::wstring::npos);
}

TEST(Models_AreFoundByFileNameInAnyCase)
{
    for (const AsrModel &m : kAsrModels) {
        CHECK(FindAsrModel(m.file) == &m);
        std::wstring shouted = m.file;
        for (wchar_t &c : shouted)
            c = c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - L'a' + L'A') : c;
        CHECK(FindAsrModel(shouted) == &m);
    }
    CHECK(FindAsrModel(L"") == nullptr);
    CHECK(FindAsrModel(L"nonsense.bin") == nullptr);
    CHECK(FindAsrModel(kVadModel.file) == nullptr);     /* a voice-activity model is not a speech model */
}

TEST(Models_OnlyEarlierInstalledOnesCanTakeOver)
{
    const AsrModel table[4] = {{L"a.bin", "x", 1, L"A", L"MIT", L"https://x"},
                               {L"b.bin", "x", 1, L"B", L"MIT", L"https://x"},
                               {L"c.bin", "x", 1, L"C", L"MIT", L"https://x"},
                               {L"d.bin", "x", 1, L"D", L"MIT", L"https://x"}};
    const std::vector<const AsrModel *> all = {&table[0], &table[1], &table[2], &table[3]};

    auto names = [](const std::vector<const AsrModel *> &models) {
        std::wstring text;
        for (const AsrModel *m : models)
            text += std::wstring(m->label);
        return text;
    };
    CHECK_EQ(names(SmallerModels(table, &table[3], all)), std::wstring(L"CBA"));             /* nearest first */
    CHECK_EQ(names(SmallerModels(table, &table[0], all)), std::wstring());                   /* nothing before the first */
    CHECK_EQ(names(SmallerModels(table, &table[2], {&table[0], &table[2], &table[3]})),
             std::wstring(L"A"));                                                            /* only those installed */
}

TEST(Models_AreListedAsTheGuiShowsThem)
{
    const TranscriptModel choice = ModelChoice(kAsrModels[1]);
    CHECK_EQ(choice.file, std::wstring(kAsrModels[1].file));
    CHECK_EQ(choice.label, std::wstring(kAsrModels[1].label));
    CHECK(choice.fixedLanguages.empty());
}

TEST(Models_OnlyWhisperCanBeToldWhichLanguageToExpect)
{
    for (const AsrModel &m : kAsrModels)                /* Whisper is told what to expect; Parakeet finds it, among the ones it knows */
        CHECK_EQ(ModelChoice(m).fixedLanguages.empty(), m.engine == AsrEngine::Whisper);
    const AsrModel *parakeet = FindAsrModel(L"ggml-parakeet-tdt-0.6b-v3-q8_0.bin");
    CHECK(parakeet && parakeet->engine == AsrEngine::Parakeet);
    CHECK(parakeet && ModelChoice(*parakeet).fixedLanguages.find(L"European") != std::wstring::npos);    /* and the window says so */
}

TEST(Models_TheTopEndIsLargeV3AndParakeetIsTheAlternative)
{
    const AsrModel *top = FindAsrModel(L"ggml-large-v3.bin");
    const AsrModel *parakeet = FindAsrModel(L"ggml-parakeet-tdt-0.6b-v3-q8_0.bin");
    CHECK(top && parakeet);
    if (!top || !parakeet)
        return;
    CHECK(top->engine == AsrEngine::Whisper && !top->fixedLanguages && top->offered);
    CHECK(top->gpuMemoryBytes >= (4ull << 30));                         /* only a PC with a card to match is offered it */
    CHECK(parakeet->engine == AsrEngine::Parakeet && parakeet->fixedLanguages && parakeet->offered);
    CHECK_EQ(parakeet->gpuMemoryBytes, std::uint64_t(0));               /* light enough for every PC */
    CHECK_EQ(std::wstring(parakeet->licence), std::wstring(L"CC-BY-4.0"));      /* NVIDIA's weights */
    for (const AsrModel &m : kAsrModels) {
        if (&m != top)
            CHECK(m.bytes <= top->bytes || m.engine != AsrEngine::Whisper);     /* of the Whisper ones, the biggest */
    }
}

TEST(Models_TheTurboModelIsUsedWhenItIsThereButNoLongerOffered)
{
    const AsrModel *turbo = FindAsrModel(L"ggml-large-v3-turbo-q5_0.bin");
    CHECK(turbo != nullptr);
    CHECK(turbo && !turbo->offered);
}

TEST(Models_TheSmallEnglishModelIsGone)
{
    CHECK(FindAsrModel(L"ggml-small.en-q5_1.bin") == nullptr);          /* a copy in the models folder is simply not used */
}

TEST(Models_AStepDownGoesToTheLighterModelsOfTheSameEngineOnly)
{
    std::vector<const AsrModel *> installed;
    for (const AsrModel &m : kAsrModels)
        installed.push_back(&m);
    const AsrModel *top = FindAsrModel(L"ggml-large-v3.bin");
    const AsrModel *turbo = FindAsrModel(L"ggml-large-v3-turbo-q5_0.bin");
    const AsrModel *parakeet = FindAsrModel(L"ggml-parakeet-tdt-0.6b-v3-q8_0.bin");
    CHECK(top && turbo && parakeet);
    if (!top || !turbo || !parakeet)
        return;
    const std::vector<const AsrModel *> lighter = SmallerModels(kAsrModels, top, installed);
    CHECK(lighter.size() == 1 && lighter[0] == turbo);                          /* every language still understood */
    CHECK(SmallerModels(kAsrModels, parakeet, installed).empty());              /* nothing lighter of its own kind, and Whisper weighs more */
}


/* ---- the manifest ---- */

TEST(Manifest_ReadsOneFilePerLine)
{
    std::vector<ManifestEntry> entries;
    CHECK(Parses(kHashA + "  audior_asr.exe\n" + kHashB + "  ggml-cpu.dll\n", &entries));
    CHECK_EQ(entries.size(), std::size_t(2));
    CHECK_EQ(entries[0].file, std::wstring(L"audior_asr.exe"));
    CHECK_EQ(entries[0].sha256, kHashA);
    CHECK_EQ(entries[1].file, std::wstring(L"ggml-cpu.dll"));
}

TEST(Manifest_AcceptsTheShaSumVariants)
{
    std::vector<ManifestEntry> entries;
    CHECK(Parses("# the pack\r\n\r\n" + kHashA + " *audior_asr.exe\r\n   \r\n" + std::string(64, 'B') + "  b.dll",
                 &entries));                            /* '*', comments, CRLF, no final newline */
    CHECK_EQ(entries.size(), std::size_t(2));
    CHECK_EQ(entries[0].file, std::wstring(L"audior_asr.exe"));
    CHECK_EQ(entries[1].sha256, kHashB);                /* upper case is read as lower case */
}

TEST(Manifest_RefusesAMalformedLine)
{
    CHECK(!Parses(""));                                 /* nothing listed */
    CHECK(!Parses("# only a comment\n"));
    CHECK(!Parses("abc  audior_asr.exe\n"));            /* a hash that is too short */
    CHECK(!Parses(kHashA + "\n"));                      /* no name */
    CHECK(!Parses(kHashA + "  \n"));
    CHECK(!Parses(std::string(63, 'a') + "g  x.exe\n"));            /* not hex */
    CHECK(!Parses(kHashA + "x.exe\n"));                 /* no separator */
    CHECK(!Parses(kHashA + "  ok.exe\n" + "oops\n"));   /* one bad line spoils the file */
}

TEST(Manifest_RefusesAnythingButAPlainFileName)
{
    for (const char *name : {"..\\evil.dll", "sub\\a.dll", "sub/a.dll", "..", ".", "C:a.dll", "\\\\server\\share\\a.dll",
                             "a?.dll", "a*.dll", "a|b.dll", "a\"b.dll", "a<b.dll", "a>b.dll"})
        CHECK(!Parses(kHashA + "  " + name + "\n"));
    CHECK(!Parses(kHashA + "  caf\xC3\xA9.dll\n"));     /* only printable ASCII */
    CHECK(!Parses(kHashA + "  with space.dll\n"));
}

TEST(Manifest_RefusesAFileListedTwice)
{
    CHECK(!Parses(kHashA + "  a.dll\n" + kHashB + "  a.dll\n"));
    CHECK(!Parses(kHashA + "  a.dll\n" + kHashB + "  A.DLL\n"));    /* Windows does not tell them apart */
}
