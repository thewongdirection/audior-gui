#include "backends.h"

#include <windows.h>

#include <algorithm>
#include <thread>

#include "ggml-backend.h"
#include "whisper.h"

namespace audiorasr {

namespace {

void Silent(ggml_log_level, const char *, void *) {}

/* What ggml_backend_load does, with a wide path: the library, its own opinion of whether this PC can use it (a score
 * of 0: no), and its registration. */
bool LoadBackend(const std::wstring &path)
{
    using Score = int (*)();
    using Init = ggml_backend_reg_t (*)();
    HMODULE library = LoadLibraryW(path.c_str());
    if (!library)
        return false;
    const Score score = reinterpret_cast<Score>(GetProcAddress(library, "ggml_backend_score"));
    const Init init = reinterpret_cast<Init>(GetProcAddress(library, "ggml_backend_init"));
    ggml_backend_reg_t reg = init && (!score || score() > 0) ? init() : nullptr;
    if (!reg) {
        FreeLibrary(library);
        return false;
    }
    ggml_backend_register(reg);
    return true;
}

}  // namespace

BackendInfo LoadBackends(const std::wstring &dir, bool gpu)
{
    BackendInfo info;
    ggml_log_set(Silent, nullptr);                      /* the helper's error output goes nowhere anyway */
    whisper_log_set(Silent, nullptr);                   /* (whisper's voice detector logs too, whichever engine is used) */
    info.usable = LoadBackend(dir + L"\\ggml-cpu.dll");
    if (gpu && info.usable)
        LoadBackend(dir + L"\\ggml-vulkan.dll");

    for (std::size_t i = 0; gpu && i < ggml_backend_dev_count(); ++i) {     /* the card the engines will pick: the first */
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        const enum ggml_backend_dev_type type = ggml_backend_dev_type(dev);
        if (type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU) {
            info.device = ggml_backend_dev_description(dev);
            info.discrete = type == GGML_BACKEND_DEVICE_TYPE_GPU;
            break;
        }
    }

    const unsigned cores = std::thread::hardware_concurrency();     /* logical: half of them are real cores */
    info.threads = cores == 0 ? 4 : static_cast<int>(std::min(8u, std::max(2u, cores / 2)));
    return info;
}

}  // namespace audiorasr
