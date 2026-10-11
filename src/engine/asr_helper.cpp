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

std::wstring Join(const std::wstring &dir, const std::wstring &name) { return dir + L"\\" + name; }

}  // namespace

bool FileSize(const std::wstring &path, std::uint64_t *bytes)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    *bytes = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

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

bool HelperInstalled(const std::wstring &programDir)
{
    std::uint64_t bytes = 0;
    return !programDir.empty() && FileSize(Join(programDir, kHelperFile), &bytes) &&
           FileSize(Join(programDir, kManifestFile), &bytes);
}

bool FetcherInstalled(const std::wstring &programDir)
{
    std::uint64_t bytes = 0;
    return !programDir.empty() && FileSize(Join(programDir, kFetcherFile), &bytes) &&
           FileSize(Join(programDir, kFetcherManifest), &bytes);
}

bool ModelIsWhole(const std::wstring &modelsDir, const AsrModel &model)
{
    std::uint64_t bytes = 0;
    return !modelsDir.empty() && FileSize(Join(modelsDir, model.file), &bytes) && bytes == model.bytes;
}

bool GpuBackendInstalled(const std::wstring &programDir)
{
    std::uint64_t bytes = 0;
    return !programDir.empty() && FileSize(Join(programDir, kGpuBackendFile), &bytes);
}

std::vector<const AsrModel *> InstalledModels(const std::wstring &modelsDir, const AsrModel *models,
                                              std::size_t modelCount)
{
    std::vector<const AsrModel *> found;
    for (std::size_t i = 0; i < modelCount; ++i) {
        if (ModelIsWhole(modelsDir, models[i]))
            found.push_back(&models[i]);
    }
    return found;
}

bool ModelInstalled(const std::wstring &modelsDir, const std::wstring &file, const AsrModel *models,
                    std::size_t modelCount)
{
    for (const AsrModel *model : InstalledModels(modelsDir, models, modelCount)) {
        if (_wcsicmp(file.c_str(), model->file) == 0)
            return true;
    }
    return false;
}

bool FindAsr(const std::wstring &programDir, const std::wstring &modelsDir, AsrSetup *setup, const AsrModel *models,
             std::size_t modelCount, const std::wstring &preferred, const AsrModel *vad)
{
    if (!HelperInstalled(programDir))
        return false;
    const std::wstring helper = Join(programDir, kHelperFile);

    for (const AsrModel *model : InstalledModels(modelsDir, models, modelCount)) {
        if (preferred.empty() || _wcsicmp(preferred.c_str(), model->file) == 0) {
            setup->directory = programDir;
            setup->helper = helper;
            setup->model = model;
            setup->modelPath = Join(modelsDir, model->file);
            setup->gpu = GpuBackendInstalled(programDir);
            setup->vad = nullptr;
            setup->vadPath.clear();
            if (vad && ModelIsWhole(modelsDir, *vad)) {
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

namespace {

bool Lists(const std::vector<ManifestEntry> &entries, const wchar_t *file)
{
    for (const ManifestEntry &entry : entries) {
        if (_wcsicmp(entry.file.c_str(), file) == 0)
            return true;
    }
    return false;
}

/* Reads the manifest `manifestFile` of `dir` and checks every file it lists.  `what` names the program the
 * files belong to, for the messages.  An empty string if all is well. */
std::wstring VerifyListed(const std::wstring &dir, const wchar_t *manifestFile, const std::wstring &what,
                          const StopCheck &stopping, std::vector<ManifestEntry> *entries)
{
    /* The manifest, which is small. */
    std::string text;
    {
        HANDLE file = CreateFileW(Join(dir, manifestFile).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return what + L"'s file list (" + std::wstring(manifestFile) + L") is missing.";
        text.resize(kMaxManifestBytes);
        DWORD got = 0;
        const BOOL read = ReadFile(file, &text[0], kMaxManifestBytes, &got, nullptr);
        CloseHandle(file);
        if (!read || got == kMaxManifestBytes)
            return what + L"'s file list could not be read.";
        text.resize(got);
    }

    std::wstring problem;
    if (!ParseManifest(text, entries, &problem))
        return problem;

    for (const ManifestEntry &entry : *entries) {
        std::string actual;
        if (!HashFile(Join(dir, entry.file), &actual, stopping))
            return what + L"'s file \"" + entry.file + L"\" is missing or cannot be read.";
        if (actual != entry.sha256)
            return what + L"'s file \"" + entry.file + L"\" is not the one in its file list.";
    }
    return std::wstring();
}

}  // namespace

std::wstring VerifyAsr(const AsrSetup &setup, bool checkModel, const StopCheck &stopping)
{
    const std::wstring what = L"The transcription engine";
    std::vector<ManifestEntry> entries;
    std::wstring problem = VerifyListed(setup.directory, kManifestFile, what, stopping, &entries);
    if (!problem.empty())
        return problem;
    if (!Lists(entries, kHelperFile))
        return what + L"'s file list does not cover " + std::wstring(kHelperFile) + L".";
    if (setup.gpu && !Lists(entries, kGpuBackendFile))  /* the helper loads this DLL by name: only a listed one */
        return what + L"'s file list does not cover " + std::wstring(kGpuBackendFile) + L".";

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

std::wstring VerifyFetcher(const std::wstring &programDir, const StopCheck &stopping)
{
    const std::wstring what = L"The download program";
    std::vector<ManifestEntry> entries;
    const std::wstring problem = VerifyListed(programDir, kFetcherManifest, what, stopping, &entries);
    if (!problem.empty())
        return problem;
    return Lists(entries, kFetcherFile) ? std::wstring()
                                        : what + L"'s file list does not cover " + std::wstring(kFetcherFile) + L".";
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
