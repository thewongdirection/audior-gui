#include "asr_helper.h"

#include <windows.h>

#include <atomic>
#include <memory>
#include <vector>

#include "../core/sha256.h"
#include "process_transport.h"

namespace audiorgui {

namespace {

constexpr DWORD kMaxManifestBytes = 64 * 1024;

bool FileSize(const std::wstring &path, std::uint64_t *bytes)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    *bytes = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

std::wstring Join(const std::wstring &dir, const std::wstring &name) { return dir + L"\\" + name; }

}  // namespace

std::wstring ProgramDirectory()
{
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return std::wstring();
    std::wstring dir(path, n);
    return dir.substr(0, dir.find_last_of(L'\\'));
}

std::wstring ModelsDirectory()
{
    wchar_t base[MAX_PATH];
    const DWORD chosen = GetEnvironmentVariableW(L"AUDIOR_MODELS", base, MAX_PATH);
    if (chosen > 0 && chosen < MAX_PATH)
        return std::wstring(base, chosen);              /* the user keeps the models somewhere else */
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return std::wstring();
    return std::wstring(base, n) + L"\\audior-gui\\models";
}

std::vector<const AsrModel *> InstalledModels(const std::wstring &modelsDir, const AsrModel *models,
                                              std::size_t modelCount)
{
    std::vector<const AsrModel *> found;
    std::uint64_t bytes = 0;
    for (std::size_t i = 0; i < modelCount && !modelsDir.empty(); ++i) {
        if (FileSize(Join(modelsDir, models[i].file), &bytes) && bytes == models[i].bytes)
            found.push_back(&models[i]);
    }
    return found;
}

bool FindAsr(const std::wstring &programDir, const std::wstring &modelsDir, AsrSetup *setup, const AsrModel *models,
             std::size_t modelCount, const std::wstring &preferred, const AsrModel *vad)
{
    std::uint64_t bytes = 0;
    const std::wstring helper = Join(programDir, kHelperFile);
    if (programDir.empty() || !FileSize(helper, &bytes) || !FileSize(Join(programDir, kManifestFile), &bytes))
        return false;

    for (const AsrModel *model : InstalledModels(modelsDir, models, modelCount)) {
        if (preferred.empty() || _wcsicmp(preferred.c_str(), model->file) == 0) {
            setup->directory = programDir;
            setup->helper = helper;
            setup->model = model;
            setup->modelPath = Join(modelsDir, model->file);
            setup->gpu = FileSize(Join(programDir, kGpuBackendFile), &bytes);
            setup->vad = nullptr;
            setup->vadPath.clear();
            if (vad && FileSize(Join(modelsDir, vad->file), &bytes) && bytes == vad->bytes) {
                setup->vad = vad;
                setup->vadPath = Join(modelsDir, vad->file);
            }
            return true;
        }
    }
    return false;
}

bool HashFile(const std::wstring &path, std::string *hex, const StopCheck &stopping)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    Sha256 hasher;
    char piece[64 * 1024];
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if ((stopping && stopping()) || !ReadFile(file, piece, sizeof(piece), &got, nullptr)) {
            ok = false;
            break;
        }
        if (got == 0)
            break;
        hasher.Update(piece, got);
    }
    CloseHandle(file);
    if (ok)
        *hex = hasher.Finish();
    return ok;
}

std::wstring VerifyAsr(const AsrSetup &setup, bool checkModel, const StopCheck &stopping)
{
    /* The manifest, which is small. */
    std::string text;
    {
        HANDLE file = CreateFileW(Join(setup.directory, kManifestFile).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return L"The transcription engine's file list (" + std::wstring(kManifestFile) + L") is missing.";
        text.resize(kMaxManifestBytes);
        DWORD got = 0;
        const BOOL read = ReadFile(file, &text[0], kMaxManifestBytes, &got, nullptr);
        CloseHandle(file);
        if (!read || got == kMaxManifestBytes)
            return L"The transcription engine's file list could not be read.";
        text.resize(got);
    }

    std::vector<ManifestEntry> entries;
    std::wstring problem;
    if (!ParseManifest(text, &entries, &problem))
        return problem;

    bool coversHelper = false;
    for (const ManifestEntry &entry : entries) {
        coversHelper = coversHelper || _wcsicmp(entry.file.c_str(), kHelperFile) == 0;
        std::string actual;
        if (!HashFile(Join(setup.directory, entry.file), &actual, stopping))
            return L"The transcription engine's file \"" + entry.file + L"\" is missing or cannot be read.";
        if (actual != entry.sha256)
            return L"The transcription engine's file \"" + entry.file + L"\" is not the one in its file list.";
    }
    if (!coversHelper)
        return L"The transcription engine's file list does not cover " + std::wstring(kHelperFile) + L".";

    if (checkModel) {
        std::string actual;
        if (!HashFile(setup.modelPath, &actual, stopping))
            return L"The model \"" + std::wstring(setup.model->file) + L"\" cannot be read.";
        if (actual != setup.model->sha256)
            return L"The model \"" + std::wstring(setup.model->file) + L"\" is damaged or is not the published file.";
        if (setup.vad) {                                /* optional, but not to be used unchecked */
            if (!HashFile(setup.vadPath, &actual, stopping))
                return L"The model \"" + std::wstring(setup.vad->file) + L"\" cannot be read.";
            if (actual != setup.vad->sha256)
                return L"The model \"" + std::wstring(setup.vad->file) +
                       L"\" is damaged or is not the published file (remove it to go without).";
        }
    }
    return std::wstring();
}

TransportFactory MakeProcessFactory(const AsrSetup &setup)
{
    auto modelChecked = std::make_shared<std::atomic<bool>>(false);
    return [setup, modelChecked](std::wstring *problem, const StopCheck &stopping) -> std::unique_ptr<IAsrTransport> {
        const std::wstring why = VerifyAsr(setup, !*modelChecked, stopping);
        if (!why.empty()) {
            *problem = why;
            return nullptr;
        }
        *modelChecked = true;
        return ProcessTransport::Launch(setup.helper, std::wstring(), ProcessTransport::Limits(), problem);
    };
}

}  // namespace audiorgui
