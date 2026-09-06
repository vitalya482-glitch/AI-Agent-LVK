#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "update/UpdateCloseBridge.h"
#include "update/UpdateManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

#ifndef AI_AGENT_LVK_VERSION
#define AI_AGENT_LVK_VERSION "0.0.0-dev"
#endif

namespace {

std::string normalizeCommand(std::string value) {
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

void printBanner() {
    std::cout
        << "========================================\n"
        << " AI-Agent-LVK v" << AI_AGENT_LVK_VERSION << "\n"
        << " Native C++ local AI agent foundation\n"
        << "========================================\n"
        << "Type 'help' to see available commands.\n\n";
}

void printHelp() {
    std::cout
        << "Commands:\n"
        << "  help     Show this help\n"
        << "  version  Show application version\n"
        << "  update   Check for updates with LVK-Updater\n"
        << "  clear    Clear the console\n"
        << "  exit     Exit AI-Agent-LVK\n";
}

} // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"AI-Agent-LVK");

    lvk::update::UpdateCloseBridge updateCloseBridge;
    if (!updateCloseBridge.start()) {
        std::cerr << "Warning: update close bridge could not be started.\n";
    }

    printBanner();

    std::string line;
    while (true) {
        std::cout << "AI> " << std::flush;

        if (!std::getline(std::cin, line)) {
            break;
        }

        const std::string command = normalizeCommand(line);

        if (command.empty()) {
            continue;
        }

        if (command == "help" || command == "?") {
            printHelp();
            continue;
        }

        if (command == "version") {
            std::cout << "AI-Agent-LVK version " << AI_AGENT_LVK_VERSION << "\n";
            continue;
        }

        if (command == "update") {
            const auto result = lvk::update::UpdateManager::launchCheck();
            if (result.ok) {
                std::cout << result.message << "\n";
            } else {
                std::cerr << "Update error: " << result.message << "\n";
            }
            continue;
        }

        if (command == "clear" || command == "cls") {
            std::system("cls");
            printBanner();
            continue;
        }

        if (command == "exit" || command == "quit") {
            break;
        }

        std::cout << "Unknown command: " << command << "\n";
        std::cout << "Type 'help' for available commands.\n";
    }

    updateCloseBridge.stop();
    return 0;
}
