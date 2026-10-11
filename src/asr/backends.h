/*
 * backends.h - Loads ggml's compute backends, which are DLLs next to the helper, and says which
 * device the engines will then compute on.
 */
#pragma once

#include <string>

namespace audiorasr {

struct BackendInfo {
    std::string device = "CPU";             /* the name of the graphics card the engine will use, or "CPU" */
    bool        discrete = false;           /* that card is one of its own (not part of the processor)     */
    int         threads = 4;                /* processor threads for the parts that run there              */
};

/* Loads the processor backend and, if `gpu`, the Vulkan one, from `dir` (UTF-8).  Only those two
 * files, by name, are ever loaded.  A missing or unusable Vulkan backend (no driver, no card) is no
 * problem: the processor does the work. */
BackendInfo LoadBackends(const std::string &dir, bool gpu);

}  // namespace audiorasr
