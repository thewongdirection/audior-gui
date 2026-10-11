# The transcription helper, audior_asr.exe.  Built by `build.bat asr`; see ../../CLAUDE.md.
#
# whisper.cpp (third_party/whisper.cpp) is built as DLLs next to the helper: ggml, whisper,
# the CPU backend (AVX2 as the baseline, no OpenMP runtime) and, if asked for with
# -DAUDIOR_ASR_VULKAN=ON (`build.bat asr vulkan`), the Vulkan backend.  The backends are
# loaded by ggml when the helper starts, so a PC without a usable Vulkan driver simply
# goes without that one and the CPU does the work.  All of them use the static C++
# runtime, so the only DLLs they need are Windows' own (and, for the Vulkan one, the
# driver's vulkan-1.dll).  build.bat lists every file in audior_asr.sha256 with its SHA-256.
cmake_minimum_required(VERSION 3.15)
project(audior_asr C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)                  # (so whisper's own, older CMake files honour the runtime below too)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

option(AUDIOR_ASR_VULKAN "Also build the Vulkan backend (needs the Vulkan SDK)" OFF)

set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL ON CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_AVX2 ON CACHE BOOL "" FORCE)
set(GGML_FMA ON CACHE BOOL "" FORCE)
set(GGML_F16C ON CACHE BOOL "" FORCE)
set(GGML_AVX512 OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN ${AUDIOR_ASR_VULKAN} CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_SDL2 OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../third_party/whisper.cpp whisper)

add_executable(audior_asr
    helper.cpp
    backends.cpp
    parakeet_decoder.cpp
    silero_classifier.cpp
    whisper_decoder.cpp
    ../core/asr_protocol.cpp
    ../core/asr_text.cpp
    ../core/decode_queue.cpp
    ../core/transcript.cpp
    ../core/utterance_tracker.cpp)
target_compile_definitions(audior_asr PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(audior_asr PRIVATE /W4 /utf-8 /EHsc)
target_link_libraries(audior_asr PRIVATE whisper parakeet)
set_target_properties(audior_asr PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)     # next to the DLLs
