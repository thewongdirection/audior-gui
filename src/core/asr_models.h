/*
 * asr_models.h - The speech models the recorder knows, and the manifest that
 * vouches for the helper's own files.
 *
 * Models are files the user downloads (they are never in the repository or
 * the program); the recorder uses one only if it is in this table, with the
 * size and SHA-256 it was published with, so a half-finished download or a
 * swapped file is not loaded.  Every entry names its licence: a model without
 * one does not belong here (a test enforces it).  Plain C++, no Windows.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "transcript.h"
#include "types.h"

namespace audiorgui {

struct AsrModel {
    const wchar_t *file;            /* the file name in the models folder                 */
    const char    *sha256;          /* 64 lower-case hex characters                       */
    std::uint64_t  bytes;
    const wchar_t *label;           /* for the user                                       */
    const wchar_t *licence;         /* of the weights, as published                       */
    const wchar_t *source;          /* where to download it from                          */
    const wchar_t *fixedLanguages = nullptr;        /* what it understands when the language cannot be chosen: it finds
                                                       it by itself; null: it takes the language it is told (or finds it) */
    std::uint64_t  gpuMemoryBytes = 0;      /* offered for download only to a PC whose graphics card has this
                                               much memory of its own; 0: offered on every PC */
    AsrEngine      engine = AsrEngine::Whisper;
    bool           offered = true;          /* false: used if the user has put it in the models folder, but never
                                               offered for download (it is big, or a matter of taste) */
};

/* In order of weight (a step down goes to the entries before); the first one installed is the one used
 * unless the person picks another. */
inline constexpr AsrModel kAsrModels[] = {
    {L"ggml-large-v3-turbo-q5_0.bin",
     "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2", 574041195,
     L"Large v3 turbo", L"MIT", L"https://huggingface.co/ggerganov/whisper.cpp", nullptr, 4ull << 30, AsrEngine::Whisper, false},
    /* The top end, offered to a PC whose card has room for it: Whisper large-v3 at full precision.  The turbo model above
     * is not offered any more (large-v3 is the better one and Parakeet the lighter) but is used when it is there. */
    {L"ggml-large-v3.bin",
     "64d182b440b98d5203c4f9bd541544d84c605196c4f7b845dfa11fb23594d1e2", 3095033483,
     L"Large v3", L"Apache-2.0", L"https://huggingface.co/ggerganov/whisper.cpp", nullptr, 8ull << 30},
    /* An alternative to Whisper: NVIDIA's Parakeet TDT 0.6B v3.  It finds the language by itself and knows only the
     * 25 European ones (no Chinese, Japanese, Arabic...), so there is none to choose; it is far lighter on the
     * processor than Whisper. */
    {L"ggml-parakeet-tdt-0.6b-v3-q8_0.bin",
     "4d64e9e96c2792186d072fde0034df0ad670cf680a2f53069052ead827fd600e", 668757119,
     L"Parakeet v3", L"CC-BY-4.0", L"https://huggingface.co/ggml-org/parakeet-GGUF", L"European languages", 0, AsrEngine::Parakeet},
};
inline constexpr std::size_t kAsrModelCount = sizeof(kAsrModels) / sizeof(kAsrModels[0]);

/* The entry of kAsrModels with this file name (any case), or null. */
const AsrModel *FindAsrModel(const std::wstring &file);

/* Where the file of `model` is downloaded from: its source with "/resolve/main/<file>" (the address
 * Hugging Face serves a file's current version from).  The size and SHA-256 above, not the address,
 * are what vouch for what arrives. */
std::wstring ModelUrl(const AsrModel &model);

/* The models of `table` that come before `chosen` and are in `installed`, the nearest first: the
 * ones to change to if `chosen` is too slow.  Only models of the same engine count (the table is
 * ordered by weight within an engine, not across them). */
std::vector<const AsrModel *> SmallerModels(const AsrModel *table, const AsrModel *chosen,
                                            const std::vector<const AsrModel *> &installed);

/* A model as the GUI lists it. */
inline TranscriptModel ModelChoice(const AsrModel &m)
{
    return {m.file, m.label, m.fixedLanguages ? m.fixedLanguages : L""};
}

/* The voice-activity model that cuts the audio into phrases; optional. */
inline constexpr AsrModel kVadModel = {
    L"ggml-silero-v5.1.2.bin",
    "29940d98d42b91fbd05ce489f3ecf7c72f0a42f027e4875919a28fb4c04ea2cf", 885098,
    L"Silero voice-activity detector", L"MIT", L"https://huggingface.co/ggml-org/whisper-vad"};

/* ---- the helper's manifest ---- */

struct ManifestEntry {
    std::string  sha256;            /* lower-case hex */
    std::wstring file;              /* a plain file name, in the helper's folder */
};

/* Reads a manifest in `sha256sum` style, one file per line:
 *
 *     <64 hex characters>  <file name>
 *
 * (a '*' before the name is allowed, blank lines and lines starting with '#'
 * are skipped).  A name must be a plain file name: no folders, no drive, no
 * "..", nothing that is not printable ASCII.  False, with the reason in
 * *problem, if any line is wrong, if a file is listed twice or if there is no
 * file at all. */
bool ParseManifest(const std::string &text, std::vector<ManifestEntry> *entries, std::wstring *problem);

}  // namespace audiorgui
