/*
 * asr_helper.h - Finds the transcription helper and a model, checks them, and
 * starts the helper for a TranscriptionHost.
 *
 * Nothing here is part of the recorder itself: the helper (audior_asr.exe and
 * the DLLs it needs, listed with their SHA-256 in audior_asr.sha256) sits in
 * the program's folder if the user has it, and models are files the user has
 * downloaded into %LOCALAPPDATA%\audior-gui\models.  Without both, transcription
 * is simply not offered.
 *
 * The checks find damage and mix-ups (a half-finished download, a helper from
 * another version, a model that is not the one in the table).  They do not
 * prove who made a file: anyone who can write to the program's folder can write
 * the manifest too.
 */
#pragma once

#include <string>
#include <vector>

#include "../core/asr_models.h"
#include "transcription_host.h"

namespace audiorgui {

inline constexpr wchar_t kHelperFile[]   = L"audior_asr.exe";
inline constexpr wchar_t kManifestFile[] = L"audior_asr.sha256";
inline constexpr wchar_t kGpuBackendFile[] = L"ggml-vulkan.dll";    /* optional: the backend for the graphics card */

/* The program that downloads a speech model (src/fetch), with the list that vouches for it.  It is
 * separate from the helper so that neither it nor the recorder needs the other; without it models are
 * added by hand. */
inline constexpr wchar_t kFetcherFile[]     = L"audior_fetch.exe";
inline constexpr wchar_t kFetcherManifest[] = L"audior_fetch.sha256";

/* What was found: the helper and the model it will be given. */
struct AsrSetup {
    std::wstring    directory;          /* the helper's folder                 */
    std::wstring    helper;             /* the helper's full path              */
    const AsrModel *model = nullptr;    /* its entry in kAsrModels             */
    std::wstring    modelPath;
    const AsrModel *vad = nullptr;      /* the voice-activity model, if it is there too (it is optional) */
    std::wstring    vadPath;
    bool            gpu = false;        /* the helper has the Vulkan backend next to it (kGpuBackendFile) */
};

/* The folder the program runs from, and the folder models are looked for in:
 * %AUDIOR_MODELS% if that is set, else %LOCALAPPDATA%\audior-gui\models. */
std::wstring ProgramDirectory();
std::wstring ModelsDirectory();

/* The size of the file, or false if there is none (or it is a folder). */
bool FileSize(const std::wstring &path, std::uint64_t *bytes);

/* Are the helper and its manifest in `programDir` (whatever models there are)?  Likewise the download
 * program and its list. */
bool HelperInstalled(const std::wstring &programDir);
bool FetcherInstalled(const std::wstring &programDir);

/* Is `model` whole in `modelsDir`?  A cheap look: only the size of the file is checked. */
bool ModelIsWhole(const std::wstring &modelsDir, const AsrModel &model);

/* Does the helper in `programDir` have the backend for the graphics card next to it (kGpuBackendFile)? */
bool GpuBackendInstalled(const std::wstring &programDir);

/* The models of the table that are whole in `modelsDir`, in the table's order.  A cheap look:
 * nothing is read, only file sizes.  (`models` is the table; the tests give it a small one.) */
std::vector<const AsrModel *> InstalledModels(const std::wstring &modelsDir, const AsrModel *models = kAsrModels,
                                              std::size_t modelCount = kAsrModelCount);

/* Is the model of the table named `file` (any case) whole in `modelsDir`? */
bool ModelInstalled(const std::wstring &modelsDir, const std::wstring &file, const AsrModel *models = kAsrModels,
                    std::size_t modelCount = kAsrModelCount);

/* Is the helper with its manifest in `programDir`, and a whole model in `modelsDir`: the file
 * named `preferred` (any case), or if that is empty the first one of the table that is there?
 * The voice-activity model `vad` is noted too if it is there whole. */
bool FindAsr(const std::wstring &programDir, const std::wstring &modelsDir, AsrSetup *setup,
             const AsrModel *models = kAsrModels, std::size_t modelCount = kAsrModelCount,
             const std::wstring &preferred = std::wstring(), const AsrModel *vad = &kVadModel);

/* The SHA-256 of a whole file, read in small pieces.  False if it cannot be
 * read, or if `stopping` (when given) says so between two pieces. */
bool HashFile(const std::wstring &path, std::string *hex, const StopCheck &stopping = nullptr);

/* Checks every file of the manifest against the folder and, if asked, the model
 * against the table.  An empty string if all is well, else why not (also when
 * `stopping` interrupted it).  It reads the files completely (a model takes a
 * second or two), so not on the UI thread. */
std::wstring VerifyAsr(const AsrSetup &setup, bool checkModel, const StopCheck &stopping = nullptr);

/* The same for the download program: its list must cover it, and it must be the file listed.  An empty
 * string if all is well, else why not. */
std::wstring VerifyFetcher(const std::wstring &programDir, const StopCheck &stopping = nullptr);

/* What a TranscriptionHost needs to start the helper: each call verifies the
 * helper's files (the model only the first time) and launches the process. */
TransportFactory MakeProcessFactory(const AsrSetup &setup);

}  // namespace audiorgui
