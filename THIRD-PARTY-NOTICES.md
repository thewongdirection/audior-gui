# Third-party notices

audior-gui itself is built only on what ships with Windows (the program that
downloads a model, `audior_fetch.exe`, uses Windows' own WinHTTP and nothing
else). The optional live transcription helper and the speech models it can use
are the exceptions. Each is used under its own licence, listed here; the models
are downloaded by the user, or by the program once the user agrees, and are not
part of this repository.

## Code

| Component | Used in | Licence | Source |
|---|---|---|---|
| whisper.cpp v1.9.5 | `whisper.dll` next to `audior_asr.exe`, in `third_party/whisper.cpp` | MIT | https://github.com/ggml-org/whisper.cpp |
| ggml (part of whisper.cpp), with its CPU and Vulkan backends | `ggml*.dll`, the same | MIT | https://github.com/ggml-org/ggml |

The MIT licence text, with the copyright notice of the authors, is in
`third_party/whisper.cpp/LICENSE`. It requires that notice to travel with copies
of the code, so a binary release of `audior_asr.exe` ships with this file.

Building the Vulkan backend (`build.bat vulkan`) uses the LunarG Vulkan SDK's headers
and shader compiler on the build machine; none of the SDK is part of this
repository or of what is built. At run time the backend uses `vulkan-1.dll`, which
the PC's graphics driver provides.

## Models

Models are not distributed with audior-gui. They are fetched from Hugging Face
(by the user, or by `audior_fetch.exe` after the user has agreed), and the recorder
uses a model only if it is one of those below, with the size and SHA-256 listed in
`src/core/asr_models.h`.

| Model | Licence | Source |
|---|---|---|
| Whisper small, English (`ggml-small.en-q5_1.bin`) | MIT | https://huggingface.co/ggerganov/whisper.cpp |
| Whisper large-v3-turbo (`ggml-large-v3-turbo-q5_0.bin`) | MIT | https://huggingface.co/ggerganov/whisper.cpp |

These are OpenAI's Whisper weights (MIT) converted to the ggml format by the
whisper.cpp project, whose repository declares the MIT licence.

The voice-activity model of the same project (`ggml-silero-v5.1.2.bin`, MIT,
https://huggingface.co/ggml-org/whisper-vad, the Silero VAD) is optional: if it is
in the models folder, the helper uses it to decide where speech is.

Adding a library or a model means adding it here, and to `third_party/README.md`
or `src/core/asr_models.h`; a test fails if a model has no licence.
