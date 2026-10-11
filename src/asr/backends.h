/*
 * backends.h - Loads ggml's compute backends, which are DLLs next to the helper, and says which
 * device the engines will then compute on.
 */
#pragma once

#include <string>

namespace audiorasr {

struct BackendInfo {
    bool        usable = false;             /* the processor backend is there: without it nothing can be computed */
    std::string device = "CPU";             /* the name of the graphics card the engine will use, or "CPU" */
    bool        discrete = false;           /* that card is one of its own (not part of the processor)     */
    int         threads = 4;                /* processor threads for the parts that run there              */
};

/* Loads the processor backend and, if `gpu`, the Vulkan one, from `dir`.  Only those two files, by name, are
 * ever loaded, and by their wide paths (ggml's own loader reads a narrow path in the system code page, which
 * finds nothing in a folder with accented or Chinese characters in its name).  A missing or unusable Vulkan
 * backend (no driver, no card) is no problem: the processor does the work. */
BackendInfo LoadBackends(const std::wstring &dir, bool gpu);

}  // namespace audiorasr
