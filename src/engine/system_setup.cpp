#include "system_setup.h"

#include <memory>
#include <utility>

#include "asr_helper.h"
#include "hardware_scan.h"
#include "model_installer.h"

namespace audiorgui {

std::unique_ptr<TranscriptionSetup> MakeSystemSetup(const std::wstring &programDir, const std::wstring &modelsDir,
                                                    bool declined)
{
    if (!HelperInstalled(programDir))
        return nullptr;

    ModelInstaller::Options options;
    options.modelsDir = modelsDir;
    options.fetcher = programDir + L"\\" + kFetcherFile;
    options.verify = [programDir] { return VerifyFetcher(programDir); };
    const std::shared_ptr<ModelInstaller> installer = std::make_shared<ModelInstaller>(std::move(options));

    TranscriptionSetup::Environment env;
    env.scan = ScanHardware;
    env.backendPresent = GpuBackendInstalled(programDir);
    env.installedModels = [modelsDir] { return InstalledModels(modelsDir); };
    env.voiceModelInstalled = [modelsDir] { return ModelIsWhole(modelsDir, kVadModel); };
    env.downloadProblem = [programDir, modelsDir]() -> std::wstring {
        if (!FetcherInstalled(programDir))
            return std::wstring(L"The download program (") + kFetcherFile + L") is not next to this program. Put a model in " +
                   modelsDir + L" by hand: the README says how.";
        return VerifyFetcher(programDir);               /* refused here, not after the user has said yes */
    };
    env.install = [installer](const AsrModel &model, const ModelInstaller::Progress &progress, const StopCheck &stopping,
                              std::wstring *problem) { return installer->Install(model, progress, stopping, problem); };
    env.cancelInstall = [installer] { installer->Cancel(); };
    env.declined = declined;
    return std::make_unique<TranscriptionSetup>(std::move(env));
}

}  // namespace audiorgui
