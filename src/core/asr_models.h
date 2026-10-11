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

namespace audiorgui {

struct AsrModel {
    const wchar_t *file;            /* the file name in the models folder                 */
    const char    *sha256;          /* 64 lower-case hex characters                       */
    std::uint64_t  bytes;
    const wchar_t *label;           /* for the user                                       */
    const wchar_t *licence;         /* of the weights, as published                       */
    const wchar_t *source;          /* where to download it from                          */
    bool           multilingual = false;    /* false: English only                        */
};

/* In order of preference for a machine without a GPU: the first one found is
 * used. */
inline constexpr AsrModel kAsrModels[] = {
    {L"ggml-small.en-q5_1.bin",
     "bfdff4894dcb76bbf647d56263ea2a96645423f1669176f4844a1bf8e478ad30", 190098681,
     L"Small · English", L"MIT", L"https://huggingface.co/ggerganov/whisper.cpp", false},
    {L"ggml-large-v3-turbo-q5_0.bin",
     "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2", 574041195,
     L"Large v3 turbo", L"MIT", L"https://huggingface.co/ggerganov/whisper.cpp", true},
};
inline constexpr std::size_t kAsrModelCount = sizeof(kAsrModels) / sizeof(kAsrModels[0]);

/* The entry of kAsrModels with this file name (any case), or null. */
const AsrModel *FindAsrModel(const std::wstring &file);

/* The models of `table` that come before `chosen` and are in `installed`, the nearest first: the
 * ones to change to if `chosen` is too slow.  With `keepLanguages` a multilingual `chosen` is
 * only replaced by multilingual models, so that no language stops being understood. */
std::vector<const AsrModel *> SmallerModels(const AsrModel *table, const AsrModel *chosen,
                                            const std::vector<const AsrModel *> &installed, bool keepLanguages);

/* A model as the GUI lists it. */
inline TranscriptModel ModelChoice(const AsrModel &m) { return {m.file, m.label, m.multilingual}; }

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
