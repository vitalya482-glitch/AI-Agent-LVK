#include "UpdateManager.h"

#ifdef _WIN32
#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
#endif

namespace lvk::update {

#ifdef _WIN32
namespace {

fs::path executableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return fs::current_path();
    }

    return fs::path(std::wstring(buffer.data(), length)).parent_path();
}

std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

} // namespace
#endif

LaunchResult UpdateManager::launchCheck() {
#ifdef _WIN32
    const fs::path appDir = executableDirectory();
    const fs::path updaterPath = appDir / L"LVKUpdater.exe";
    const fs::path configPath = appDir / L"app.update.json";

    std::error_code ec;
    if (!fs::exists(updaterPath, ec)) {
        return {false, "LVKUpdater.exe was not found next to AI-Agent-LVK.exe."};
    }

    if (!fs::exists(configPath, ec)) {
        return {false, "app.update.json was not found next to AI-Agent-LVK.exe."};
    }

    const DWORD pid = GetCurrentProcessId();

    std::wstring commandLine =
        quote(updaterPath.wstring()) +
        L" --check" +
        L" --app-dir " + quote(appDir.wstring()) +
        L" --config " + quote(configPath.wstring()) +
        L" --app-pid " + std::to_wstring(pid);

    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo{};

    const BOOL created = CreateProcessW(
        updaterPath.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        appDir.c_str(),
        &startupInfo,
        &processInfo);

    if (!created) {
        return {
            false,
            "Failed to launch LVKUpdater.exe. Win32 error: " + std::to_string(GetLastError())
        };
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return {
        true,
        "Update check started. The app will close automatically only if an update is confirmed and ready to install."
    };
#else
    return {
        false,
        "LVK-Updater integration is currently Windows-only. The core API/network layer is portable."
    };
#endif
}

} // namespace lvk::update
