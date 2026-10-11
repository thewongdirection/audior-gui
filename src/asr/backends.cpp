#include "backends.h"

#include <algorithm>
#include <thread>

#include "ggml-backend.h"
#include "whisper.h"

namespace audiorasr {

namespace {

void Silent(ggml_log_level, const char *, void *) {}

}  // namespace

BackendInfo LoadBackends(const std::string &dir, bool gpu)
{
    BackendInfo info;
    ggml_log_set(Silent, nullptr);                      /* the helper's error output goes nowhere anyway */
    whisper_log_set(Silent, nullptr);                   /* (whisper's voice detector logs too, whichever engine is used) */
    ggml_backend_load((dir + "\\ggml-cpu.dll").c_str());
    if (gpu)
        ggml_backend_load((dir + "\\ggml-vulkan.dll").c_str());

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
