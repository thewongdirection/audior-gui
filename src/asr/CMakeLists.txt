# The transcription helper, audior_asr.exe.  Built by `build.bat asr`; see ../../CLAUDE.md.
#
# whisper.cpp (third_party/whisper.cpp) is compiled in statically: CPU only, no
# OpenMP runtime, the AVX2 instruction set as the baseline, and the static C++
# runtime, so the helper needs nothing but kernel32.
cmake_minimum_required(VERSION 3.15)
project(audior_asr C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)                  # (so whisper's own, older CMake files honour the runtime below too)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_AVX2 ON CACHE BOOL "" FORCE)
set(GGML_FMA ON CACHE BOOL "" FORCE)
set(GGML_F16C ON CACHE BOOL "" FORCE)
set(GGML_AVX512 OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_SDL2 OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../third_party/whisper.cpp whisper)

add_executable(audior_asr
    helper.cpp
    whisper_decoder.cpp
    ../core/asr_protocol.cpp
    ../core/asr_text.cpp
    ../core/decode_queue.cpp
    ../core/transcript.cpp
    ../core/utterance_tracker.cpp)
target_compile_definitions(audior_asr PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(audior_asr PRIVATE /W4 /utf-8 /EHsc)
target_link_libraries(audior_asr PRIVATE whisper)
set_target_properties(audior_asr PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
