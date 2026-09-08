#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "update/UpdateCloseBridge.h"
#endif

#include "api/ApiServer.h"
#include "core/AppConfig.h"
#include "core/CommandDispatcher.h"

#include <cstdlib>
#include <iostream>
#include <string>

#ifndef AI_AGENT_LVK_VERSION
#define AI_AGENT_LVK_VERSION "0.0.0-dev"
#endif

namespace {

void printBanner() {
    std::cout
        << "========================================\n"
        << " AI-Agent-LVK v" << AI_AGENT_LVK_VERSION << "\n"
        << " Native C++ local AI agent foundation\n"
        << "========================================\n"
        << "Type 'help' to see available commands.\n\n";
}

void clearConsole() {
#ifdef _WIN32
    std::system("cls");
#else
    std::system("clear");
#endif
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleW(L"AI-Agent-LVK");

    lvk::update::UpdateCloseBridge updateCloseBridge;
    if (!updateCloseBridge.start()) {
        std::cerr << "Warning: update close bridge could not be started.\n";
    }
#endif

    lvk::core::CommandDispatcher dispatcher(AI_AGENT_LVK_VERSION);
    lvk::api::ApiServer apiServer(
        dispatcher,
        lvk::core::kDefaultApiHost,
        lvk::core::kDefaultApiPort);

    printBanner();

    if (apiServer.start()) {
        std::cout
            << "API server listening on http://"
            << lvk::core::kDefaultApiHost << ':'
            << lvk::core::kDefaultApiPort
            << "/api/v1\n\n";
    } else {
        std::cerr
            << "Warning: API server could not bind to "
            << lvk::core::kDefaultApiHost << ':'
            << lvk::core::kDefaultApiPort
            << ". Console mode will continue.\n\n";
    }

    std::string line;
    while (true) {
        std::cout << "AI> " << std::flush;

        if (!std::getline(std::cin, line)) {
            break;
        }

        const std::string command = lvk::core::CommandDispatcher::normalizeCommand(line);
        if (command.empty()) {
            continue;
        }

        if (command == "clear" || command == "cls") {
            clearConsole();
            printBanner();
            continue;
        }

        if (command == "exit" || command == "quit") {
            break;
        }

        const auto result = dispatcher.execute(command);
        if (result.ok) {
            std::cout << result.output << "\n";
        } else {
            std::cerr << result.output << "\n";
        }
    }

    apiServer.stop();

#ifdef _WIN32
    updateCloseBridge.stop();
#endif

    return 0;
}
