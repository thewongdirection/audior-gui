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
| whisper.cpp v1.9.5 | `whisper.dll` and `parakeet.dll` next to `audior_asr.exe`, in `third_party/whisper.cpp` | MIT | https://github.com/ggml-org/whisper.cpp |
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
| Whisper large-v3-turbo (`ggml-large-v3-turbo-q5_0.bin`) | MIT | https://huggingface.co/ggerganov/whisper.cpp |
| Whisper large-v3 (`ggml-large-v3.bin`) | Apache-2.0 | https://huggingface.co/ggerganov/whisper.cpp |
| Parakeet TDT 0.6B v3 (`ggml-parakeet-tdt-0.6b-v3-q8_0.bin`) | CC BY 4.0 | https://huggingface.co/ggml-org/parakeet-GGUF |

The Whisper ones are OpenAI's weights converted to the ggml format by the whisper.cpp
project, whose repository declares the MIT licence. That of turbo is
published by OpenAI under the MIT licence; those of large-v3 under the Apache Licence
2.0 (https://huggingface.co/openai/whisper-large-v3).

Parakeet TDT 0.6B v3 is NVIDIA's model (https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3),
licensed under the Creative Commons Attribution 4.0 International licence
(https://creativecommons.org/licenses/by/4.0/). The ggml-org project converted it to
the format whisper.cpp reads and quantized it to 8 bits (their repository's own files
are declared MIT; the weights stay NVIDIA's under CC BY 4.0). It is run by the parakeet
library of whisper.cpp, which is part of the code above. The program only reads this
file; it does not change or redistribute it.

The voice-activity model of the same project (`ggml-silero-v5.1.2.bin`, MIT,
https://huggingface.co/ggml-org/whisper-vad, the Silero VAD) is optional: if it is
in the models folder, the helper uses it to decide where speech is.

Adding a library or a model means adding it here, and to `third_party/README.md`
or `src/core/asr_models.h`; a test fails if a model has no licence.
