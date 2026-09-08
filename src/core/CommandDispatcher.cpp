#include "core/CommandDispatcher.h"

#include "core/AppConfig.h"
#include "update/UpdateManager.h"

#include <algorithm>
#include <cctype>
#include <sstream>
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
            "  clear    Clear the console (console only)\n"
            "  exit     Exit AI-Agent-LVK (console only)"
        };
    }

    if (normalized == "version") {
        return {true, "AI-Agent-LVK version " + version_};
    }

    if (normalized == "status") {
        std::ostringstream output;
        output
            << "Core: running\n"
            << "API: http://" << kDefaultApiHost << ':' << kDefaultApiPort << "/api/v1\n"
            << "Model: not loaded\n"
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

    return {false, "Unknown command: " + normalized};
}

const std::string& CommandDispatcher::version() const noexcept {
    return version_;
}

} // namespace lvk::core
