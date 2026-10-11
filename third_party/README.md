# Third-party code

The recorder itself (`audior_gui.exe`) uses nothing but what ships with Windows.
The optional transcription helper (`audior_asr.exe`, built from `src/asr`) is the
one exception, and this folder is where its code lives. See `CLAUDE.md` for the
rule and `THIRD-PARTY-NOTICES.md` for every licence.

## whisper.cpp

| | |
|---|---|
| Upstream | https://github.com/ggml-org/whisper.cpp |
| Version | v1.9.5 (tag), commit `d1be6fde11ac6e0407606b4e42fe72d34add8037` |
| Licence | MIT, in `whisper.cpp/LICENSE` |
| Used for | speech recognition in `audior_asr.exe`, linked statically, CPU only |

It is a trimmed copy: only what a CPU build for Windows on x86 needs. Left out are
everything outside `src/`, `include/`, `ggml/` and `cmake/` (examples, bindings,
tests, models, scripts), every GPU and accelerator backend of ggml
(`ggml/src/ggml-cuda`, `-vulkan`, `-metal` and so on), the CPU backend's code for
other processors (`arch/arm`, `arch/riscv`, ...) and the Core ML, OpenVINO and
Vitis AI parts of `src/`. (`src/aneforge` stays: upstream's CMake lists its
files unconditionally, and they compile to nothing outside Apple platforms.)
Nothing that remains has been edited.

To update, take the new tag, repeat that trimming, change the version and commit
above, and run `build.bat asr` and the tests. Models are never part of the
repository: users download them (see `src/core/asr_models.h`).
