#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include "update/UpdateCloseBridge.h"
#endif

#include "api/ApiServer.h"
#include "core/AppConfig.h"
#include "core/CommandDispatcher.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#ifndef AI_AGENT_LVK_VERSION
#define AI_AGENT_LVK_VERSION "0.0.0-dev"
#endif

namespace {

bool hasArg(int argc, char** argv, const char* wanted) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], wanted) == 0) {
            return true;
        }
    }
    return false;
}

#ifdef _WIN32
bool launchedByUpdater() {
    const DWORD selfPid = GetCurrentProcessId();
    DWORD parentPid = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == selfPid) {
                parentPid = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    bool result = false;
    if (parentPid != 0) {
        entry = {};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (entry.th32ProcessID == parentPid) {
                    result = (_wcsicmp(entry.szExeFile, L"LVKUpdater.exe") == 0);
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
    }

    CloseHandle(snapshot);
    return result;
}
#endif

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

int main(int argc, char** argv) {
    bool headless = hasArg(argc, argv, "--headless");

#ifdef _WIN32
    if (!headless && launchedByUpdater()) {
        headless = true;
        if (const HWND console = GetConsoleWindow(); console != nullptr) {
            ShowWindow(console, SW_HIDE);
        }
        FreeConsole();
    }

    if (!headless) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        SetConsoleTitleW(L"AI-Agent-LVK");
    }

    lvk::update::UpdateCloseBridge updateCloseBridge;
    if (!updateCloseBridge.start() && !headless) {
        std::cerr << "Warning: update close bridge could not be started.\n";
    }
#endif

    lvk::core::CommandDispatcher dispatcher(AI_AGENT_LVK_VERSION);
    lvk::api::ApiServer apiServer(
        dispatcher,
        lvk::core::kDefaultApiHost,
        lvk::core::kDefaultApiPort);

    if (!headless) {
        printBanner();
    }

    const bool apiStarted = apiServer.start();
    if (apiStarted) {
        if (!headless) {
            std::cout
                << "API server listening on http://"
                << lvk::core::kDefaultApiHost << ':'
                << lvk::core::kDefaultApiPort
                << "/api/v1\n\n";
        }
    } else {
        if (!headless) {
            std::cerr
                << "Warning: API server could not bind to "
                << lvk::core::kDefaultApiHost << ':'
                << lvk::core::kDefaultApiPort
                << ". Console mode will continue.\n\n";
        } else {
#ifdef _WIN32
            updateCloseBridge.stop();
#endif
            return 2;
        }
    }

    if (headless) {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
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
