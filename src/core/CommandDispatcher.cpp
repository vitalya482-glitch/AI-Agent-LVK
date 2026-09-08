#include "core/CommandDispatcher.h"

#include "core/AppConfig.h"
#include "update/UpdateManager.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <utility>

namespace lvk::core {

CommandDispatcher::CommandDispatcher(std::string version)
    : version_(std::move(version)) {
}

std::string CommandDispatcher::normalizeCommand(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);

    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    return value;
}

CommandResult CommandDispatcher::execute(const std::string& command) const {
    const auto first = command.find_first_not_of(" \t\r\n");
    const std::string raw = first == std::string::npos ? "" : command.substr(first);
    const std::string normalized = normalizeCommand(command);

    if (normalized.empty()) {
        return {false, "Command is empty."};
    }

    if (normalized == "help" || normalized == "?") {
        return {
            true,
            "Commands:\n"
            "  help     Show this help\n"
            "  version  Show application version\n"
            "  status   Show core/API/model/agent status\n"
            "  ping     Test the command dispatcher\n"
            "  update   Check for updates with LVK-Updater\n"
            "  model status                 Show model backend/configuration\n"
            "  model config <ctx> <threads> <gpu_layers>\n"
            "  model load <path-to-model.gguf>\n"
            "  chat <message>               Generate a response\n"
            "  clear    Clear the console (console only)\n"
            "  exit     Exit AI-Agent-LVK (console only)"
        };
    }

    if (normalized == "version") {
        return {true, "AI-Agent-LVK version " + version_};
    }

    if (normalized == "status") {
        const auto model = modelRuntime_.status();
        std::ostringstream output;
        output
            << "Core: running\n"
            << "API: http://" << kDefaultApiHost << ':' << kDefaultApiPort << "/api/v1\n"
            << "Model: " << (model.loaded ? model.description : "not loaded") << "\n"
            << "Agents: 0";
        return {true, output.str()};
    }

    if (normalized == "ping") {
        return {true, "pong"};
    }

    if (normalized == "update") {
        const auto result = lvk::update::UpdateManager::launchCheck();
        return {result.ok, result.message};
    }

    if (normalized == "model status") {
        const auto model = modelRuntime_.status();
        std::ostringstream output;
        const auto mib = [](std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
        output << "Model: " << (model.loaded ? "loaded" : "not loaded") << "\n"
               << "GPU backend: " << (model.gpuAvailable ? "available" : "not available") << "\n"
               << "Context: " << model.config.contextSize << "\n"
               << "Threads: " << model.config.threadCount << "\n"
               << "GPU layers: " << model.config.gpuLayers;
        if (model.loaded) {
            output << "\nModel layers: " << model.modelLayers
                   << "\nGPU weight sections: " << model.gpuLayersLoaded
                   << "\nModel size: " << mib(model.modelSizeBytes) << " MiB"
                   << "\nParameters: " << model.parameterCount
                   << "\nCPU/RAM weights (estimate): " << mib(model.cpuWeightBytesEstimate) << " MiB"
                   << "\nGPU/VRAM weights (estimate): " << mib(model.gpuWeightBytesEstimate) << " MiB"
                   << "\nContext used: " << model.contextTokensUsed << '/' << model.config.contextSize << " tokens";
        }
        return {true, output.str()};
    }

    constexpr std::string_view loadPrefix = "model load ";
    if (raw.starts_with(loadPrefix)) {
        std::string path = raw.substr(loadPrefix.size());
        const auto pathFirst = path.find_first_not_of(" \t\r\n");
        if (pathFirst == std::string::npos) {
            return {false, "Provide a path to a GGUF model."};
        }
        path = path.substr(pathFirst);
        const auto pathLast = path.find_last_not_of(" \t\r\n");
        path.resize(pathLast + 1);
        if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
            path = path.substr(1, path.size() - 2);
        }
        const auto result = modelRuntime_.load(path);
        return {result.ok, result.message};
    }

    constexpr std::string_view configPrefix = "model config ";
    if (normalized.starts_with(configPrefix)) {
        std::istringstream input(normalized.substr(configPrefix.size()));
        model::ModelConfig config;
        if (!(input >> config.contextSize >> config.threadCount >> config.gpuLayers) ||
            (input >> std::ws && !input.eof())) {
            return {false, "Usage: model config <context> <threads> <gpu_layers>"};
        }
        const auto result = modelRuntime_.configure(config);
        return {result.ok, result.message};
    }
    constexpr std::string_view chatPrefix = "chat ";
    if (raw.starts_with(chatPrefix)) {
        return chat(raw.substr(chatPrefix.size()));
    }

    return {false, "Unknown command: " + normalized};
}

CommandResult CommandDispatcher::chat(const std::string& message) const {
    std::string response;
    const auto result = modelRuntime_.chat(message, response);
    return {result.ok, result.ok ? response : result.message};
}

const std::string& CommandDispatcher::version() const noexcept {
    return version_;
}

} // namespace lvk::core
