/* Tests of finding and verifying the transcription helper, in scratch folders with tiny stand-in files. */
#include <string>

#include "../src/core/sha256.h"
#include "../src/engine/asr_helper.h"
#include "temp_dir.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

void Write(const std::wstring &path, const std::string &content)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(h != INVALID_HANDLE_VALUE);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(h);
}

/* A stand-in model: a few bytes with their real hash. */
struct StandIn {
    std::string content = "pretend these are the weights";
    std::string hash = Sha256Hex(content.data(), content.size());
    AsrModel    model;

    StandIn()
    {
        model = {L"tiny.bin", nullptr, content.size(), L"A test model", L"MIT", L"https://example.test"};
        model.sha256 = hash.c_str();
    }
};

/* A program folder with the helper, a DLL and a manifest that vouches for both. */
struct Pack {
    TempDir     programDir, modelsDir;
    StandIn     standIn;
    std::string helperContent = "MZ pretend helper", dllContent = "MZ pretend backend";

    Pack()
    {
        Write(programDir.Path(kHelperFile), helperContent);
        Write(programDir.Path(L"ggml-cpu.dll"), dllContent);
        WriteManifest();
        Write(modelsDir.Path(standIn.model.file), standIn.content);
    }

    void WriteManifest(const std::string &extra = std::string())
    {
        Write(programDir.Path(kManifestFile),
              Sha256Hex(helperContent.data(), helperContent.size()) + "  audior_asr.exe\n" +
                  Sha256Hex(dllContent.data(), dllContent.size()) + " *ggml-cpu.dll\n" + extra);
    }

    bool Find(AsrSetup *setup) const
    {
        return FindAsr(programDir.Dir(), modelsDir.Dir(), setup, &standIn.model, 1);
    }
};

}  // namespace

TEST(Asr_HashFileMatchesTheDigestOfItsContent)
{
    TempDir dir;
    Write(dir.Path(L"abc.txt"), "abc");
    std::string hex;
    CHECK(HashFile(dir.Path(L"abc.txt"), &hex));
    CHECK_EQ(hex, std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    Write(dir.Path(L"empty.txt"), "");
    CHECK(HashFile(dir.Path(L"empty.txt"), &hex));
    CHECK_EQ(hex, std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    CHECK(!HashFile(dir.Path(L"missing.txt"), &hex));
}

TEST(Asr_HashingGivesUpWhenAskedTo)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    std::string hex;
    CHECK(!HashFile(setup.modelPath, &hex, [] { return true; }));
    CHECK(!VerifyAsr(setup, true, [] { return true; }).empty());
    CHECK(VerifyAsr(setup, true, [] { return false; }).empty());
}

TEST(Asr_HashFileReadsMoreThanOnePiece)
{
    TempDir dir;
    const std::string content(200 * 1000, 'q');          /* several 64 KB pieces */
    Write(dir.Path(L"big.bin"), content);
    std::string hex;
    CHECK(HashFile(dir.Path(L"big.bin"), &hex));
    CHECK_EQ(hex, Sha256Hex(content.data(), content.size()));
}

TEST(Asr_FindsTheHelperAndAWholeModel)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    CHECK_EQ(setup.helper, pack.programDir.Path(kHelperFile));
    CHECK_EQ(setup.modelPath, pack.modelsDir.Path(L"tiny.bin"));
    CHECK(setup.model == &pack.standIn.model);
}

TEST(Asr_NothingIsFoundWithoutEveryPiece)
{
    AsrSetup setup;
    CHECK(!FindAsr(L"", L"", &setup));                  /* unknown folders */

    {
        TempDir empty, models;
        CHECK(!FindAsr(empty.Dir(), models.Dir(), &setup));
    }
    {
        Pack pack;                                      /* no helper */
        DeleteFileW(pack.programDir.Path(kHelperFile).c_str());
        CHECK(!pack.Find(&setup));
    }
    {
        Pack pack;                                      /* no manifest */
        DeleteFileW(pack.programDir.Path(kManifestFile).c_str());
        CHECK(!pack.Find(&setup));
    }
    {
        Pack pack;                                      /* no model */
        DeleteFileW(pack.modelsDir.Path(L"tiny.bin").c_str());
        CHECK(!pack.Find(&setup));
    }
    {
        Pack pack;                                      /* a model that is not whole (a download in progress) */
        Write(pack.modelsDir.Path(L"tiny.bin"), pack.standIn.content.substr(0, 5));
        CHECK(!pack.Find(&setup));
    }
}

TEST(Asr_InstalledModelsAreTheWholeOnesInTheTablesOrder)
{
    const AsrModel table[2] = {{L"a.bin", "x", 3, L"A", L"MIT", L"https://example.test"},
                               {L"b.bin", "x", 4, L"B", L"MIT", L"https://example.test"}};
    TempDir models;
    CHECK(InstalledModels(models.Dir(), table, 2).empty());
    CHECK(InstalledModels(std::wstring(), table, 2).empty());

    Write(models.Path(L"b.bin"), "1234");
    std::vector<const AsrModel *> found = InstalledModels(models.Dir(), table, 2);
    CHECK_EQ(found.size(), std::size_t(1));
    CHECK(found.size() == 1 && found[0] == &table[1]);

    Write(models.Path(L"a.bin"), "123");
    found = InstalledModels(models.Dir(), table, 2);
    CHECK(found.size() == 2 && found[0] == &table[0] && found[1] == &table[1]);

    Write(models.Path(L"a.bin"), "12");                 /* not whole: a download in progress */
    found = InstalledModels(models.Dir(), table, 2);
    CHECK(found.size() == 1 && found[0] == &table[1]);
}

TEST(Asr_TheModelAskedForIsTheOneFound)
{
    const AsrModel table[2] = {{L"a.bin", "x", 3, L"A", L"MIT", L"https://example.test"},
                               {L"b.bin", "x", 4, L"B", L"MIT", L"https://example.test"}};
    TempDir programs, models;
    Write(programs.Path(kHelperFile), "x");
    Write(programs.Path(kManifestFile), "x");
    Write(models.Path(L"a.bin"), "123");
    Write(models.Path(L"b.bin"), "1234");

    AsrSetup setup;
    CHECK(FindAsr(programs.Dir(), models.Dir(), &setup, table, 2));                 /* none asked for: the first */
    CHECK(setup.model == &table[0]);
    CHECK(FindAsr(programs.Dir(), models.Dir(), &setup, table, 2, L"B.BIN"));       /* any case */
    CHECK(setup.model == &table[1]);
    CHECK_EQ(setup.modelPath, models.Path(L"b.bin"));

    CHECK(!FindAsr(programs.Dir(), models.Dir(), &setup, table, 2, L"c.bin"));      /* not a model of the table */
    Write(models.Path(L"b.bin"), "12");
    CHECK(!FindAsr(programs.Dir(), models.Dir(), &setup, table, 2, L"b.bin"));      /* asked for, but not whole */
    CHECK(FindAsr(programs.Dir(), models.Dir(), &setup, table, 2));                 /* the first is still fine  */
}

TEST(Asr_TheGraphicsCardBackendIsNotedWhenItIsThere)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    CHECK(!setup.gpu);                                  /* a helper without the Vulkan backend: the processor only */

    Write(pack.programDir.Path(kGpuBackendFile), "MZ pretend backend");
    CHECK(pack.Find(&setup));
    CHECK(setup.gpu);

    DeleteFileW(pack.programDir.Path(kGpuBackendFile).c_str());
    CHECK(pack.Find(&setup));
    CHECK(!setup.gpu);                                  /* and it is looked for again each time */
}

TEST(Asr_TheVoiceActivityModelIsNotedOnlyWhenItIsWhole)
{
    Pack pack;
    const std::string vadContent = "pretend voice-activity weights", vadHash = Sha256Hex(vadContent.data(), vadContent.size());
    AsrModel vad = {L"vad.bin", vadHash.c_str(), vadContent.size(), L"A test detector", L"MIT", L"https://example.test"};
    auto find = [&](AsrSetup *setup) {
        return FindAsr(pack.programDir.Dir(), pack.modelsDir.Dir(), setup, &pack.standIn.model, 1, std::wstring(), &vad);
    };

    AsrSetup setup;
    CHECK(find(&setup));                                /* it is optional: no file, no detector */
    CHECK(setup.vad == nullptr && setup.vadPath.empty());

    Write(pack.modelsDir.Path(L"vad.bin"), vadContent.substr(0, 4));
    CHECK(find(&setup) && setup.vad == nullptr);        /* not whole */

    Write(pack.modelsDir.Path(L"vad.bin"), vadContent);
    CHECK(find(&setup));
    CHECK(setup.vad == &vad);
    CHECK_EQ(setup.vadPath, pack.modelsDir.Path(L"vad.bin"));
    CHECK(VerifyAsr(setup, true).empty());

    Write(pack.modelsDir.Path(L"vad.bin"), std::string(vadContent.size(), 'x'));    /* the right size, other content */
    CHECK(find(&setup) && setup.vad == &vad);
    const std::wstring why = VerifyAsr(setup, true);
    CHECK(why.find(L"vad.bin") != std::wstring::npos);
    CHECK(VerifyAsr(setup, false).empty());             /* checked together with the model, once */

    CHECK(FindAsr(pack.programDir.Dir(), pack.modelsDir.Dir(), &setup, &pack.standIn.model, 1, std::wstring(), nullptr));
    CHECK(setup.vad == nullptr);                        /* a caller that wants none gets none */
}

TEST(Asr_TheRealTableIsNotFoundInAnEmptyFolder)
{
    TempDir programs, models;
    Write(programs.Path(kHelperFile), "x");
    Write(programs.Path(kManifestFile), "x");
    AsrSetup setup;
    CHECK(!FindAsr(programs.Dir(), models.Dir(), &setup));
}

TEST(Asr_AnIntactPackVerifies)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    CHECK(VerifyAsr(setup, true).empty());
    CHECK(VerifyAsr(setup, false).empty());
}

TEST(Asr_AChangedHelperFileIsRefused)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    Write(pack.programDir.Path(kHelperFile), "MZ a different helper");
    const std::wstring why = VerifyAsr(setup, false);
    CHECK(why.find(L"audior_asr.exe") != std::wstring::npos);
}

TEST(Asr_AChangedBackendDllIsRefused)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    Write(pack.programDir.Path(L"ggml-cpu.dll"), "MZ planted");
    CHECK(VerifyAsr(setup, false).find(L"ggml-cpu.dll") != std::wstring::npos);
}

TEST(Asr_AMissingListedFileIsRefused)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    DeleteFileW(pack.programDir.Path(L"ggml-cpu.dll").c_str());
    CHECK(VerifyAsr(setup, false).find(L"ggml-cpu.dll") != std::wstring::npos);
}

TEST(Asr_AManifestThatDoesNotCoverTheHelperIsRefused)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    Write(pack.programDir.Path(kManifestFile),
          Sha256Hex(pack.dllContent.data(), pack.dllContent.size()) + "  ggml-cpu.dll\n");
    CHECK(!VerifyAsr(setup, false).empty());
}

TEST(Asr_AManifestNamingAFileOutsideTheFolderIsRefused)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    pack.WriteManifest(std::string(64, 'a') + "  ..\\elsewhere.dll\n");
    CHECK(!VerifyAsr(setup, false).empty());
}

TEST(Asr_ALostOrCorruptManifestIsRefused)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    Write(pack.programDir.Path(kManifestFile), "this is not a manifest\n");
    CHECK(!VerifyAsr(setup, false).empty());
    DeleteFileW(pack.programDir.Path(kManifestFile).c_str());
    CHECK(!VerifyAsr(setup, false).empty());
}

TEST(Asr_AnOversizedManifestIsRefused)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    Write(pack.programDir.Path(kManifestFile), std::string(70 * 1024, '#'));
    CHECK(!VerifyAsr(setup, false).empty());
}

TEST(Asr_ADamagedModelIsRefusedOnlyWhenChecked)
{
    Pack pack;
    AsrSetup setup;
    CHECK(pack.Find(&setup));
    std::string damaged = pack.standIn.content;
    damaged[3] = '#';                                   /* same size, one byte different */
    Write(pack.modelsDir.Path(L"tiny.bin"), damaged);

    CHECK(VerifyAsr(setup, false).empty());             /* the model is not read unless asked */
    const std::wstring why = VerifyAsr(setup, true);
    CHECK(why.find(L"tiny.bin") != std::wstring::npos);
}

TEST(Asr_ProgramAndModelFoldersAreFullPaths)
{
    const std::wstring program = ProgramDirectory();
    CHECK(program.size() > 3 && program[1] == L':');
    const std::wstring models = ModelsDirectory();
    CHECK(models.empty() || models.find(L"audior-gui") != std::wstring::npos);
}
