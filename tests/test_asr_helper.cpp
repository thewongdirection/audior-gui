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
