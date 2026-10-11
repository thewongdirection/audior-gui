/*
 * fake_fetch.h - run_tests.exe pretending to be the download program.
 *
 * The tests of ModelInstaller need a real child process that behaves like
 * audior_fetch.exe.  Rather than build a second program, run_tests.exe starts
 * itself with `--fake-fetch <mode> <url> <file> <bytes>` (the three arguments the
 * installer always gives, after the flag and the mode) and plays the program:
 *
 *   ok       checks what it was given (an https address of the file's name, a .part file),
 *            writes the content (FakeFetchContent), says how far it has got, says it is done
 *   short    writes one byte too few and says it is done all the same
 *   corrupt  writes the right number of bytes, the first of them wrong, and says it is done
 *   error    writes a little, says "E no route" and exits
 *   die      writes a little and exits without a word
 *   hang     writes a little, says how far it has got, and waits until it is killed
 */
#pragma once

#include <cstddef>
#include <string>

inline constexpr char kFakeFetchFlag[] = "--fake-fetch";

/* What a file of `bytes` bytes holds when the download goes well. */
std::string FakeFetchContent(std::size_t bytes);

/* Plays the program in `mode` with its three arguments; the return value is the process's exit code. */
int RunFakeFetch(const char *mode, const char *url, const char *file, const char *bytes);
