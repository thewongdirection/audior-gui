/*
 * fake_asr_helper.h - run_tests.exe pretending to be the transcription helper.
 *
 * The tests of ProcessTransport need a real child process that speaks the
 * helper's protocol.  Rather than build a second program, run_tests.exe starts
 * itself with `--fake-asr-helper <mode>` and plays the helper:
 *
 *   echo   Ready (saying its priority class), then an Event per Audio message
 *          ("samples=<total so far>"), and on Stop a final Event "bye" and exit
 *   spawn  like echo, but first tries to start another process and says whether
 *          it could ("spawn=ok" or "spawn=blocked") in its Ready text
 *   crash  exits at once with code 3
 *   hang   neither reads nor writes, until it is killed
 */
#pragma once

inline constexpr char kFakeAsrHelperFlag[] = "--fake-asr-helper";

/* Plays the helper in `mode` on this process's standard input and output; the
 * return value is the process's exit code. */
int RunFakeAsrHelper(const char *mode);
