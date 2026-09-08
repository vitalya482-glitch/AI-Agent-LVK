#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ApiClient.h"
#include "core/AppConfig.h"

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef AI_AGENT_LVK_VERSION
#define AI_AGENT_LVK_VERSION "0.0.0-dev"
#endif

namespace {

constexpr int kStatusId = 1001;
constexpr int kHistoryId = 1002;
constexpr int kInputId = 1003;
constexpr int kSendId = 1004;
constexpr int kStartCoreId = 1005;
constexpr int kRestartCoreId = 1006;
constexpr int kUpdateId = 1007;
constexpr UINT_PTR kStatusTimerId = 1;
constexpr UINT kStatusPollMs = 2000;

constexpr wchar_t kCoreBridgeClass[] = L"AI_AGENT_LVK_UPDATE_BRIDGE";

HWND gStatus = nullptr;
HWND gHistory = nullptr;
HWND gInput = nullptr;
HWND gSend = nullptr;
HWND gStartCore = nullptr;
HWND gRestartCore = nullptr;
HWND gUpdate = nullptr;

bool gConnected = false;
bool gRestartPending = false;

lvk::gui::ApiClient gApi(lvk::core::kDefaultApiHost, lvk::core::kDefaultApiPort);

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return L"[invalid UTF-8]";
    }

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int length = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring getText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }

    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
    GetWindowTextW(control, buffer.data(), length + 1);
    return std::wstring(buffer.data(), static_cast<std::size_t>(length));
}

void appendHistory(const std::wstring& text) {
    const LRESULT length = SendMessageW(gHistory, WM_GETTEXTLENGTH, 0, 0);
    SendMessageW(gHistory, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    SendMessageW(gHistory, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

std::string jsonEscape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);

    for (const char ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(ch); break;
        }
    }

    return result;
}

bool extractJsonString(const std::string& json, std::string_view field, std::string& value) {
    const std::string token = "\"" + std::string(field) + "\"";
    const auto fieldPos = json.find(token);
    if (fieldPos == std::string::npos) {
        return false;
    }

    const auto colon = json.find(':', fieldPos + token.size());
    if (colon == std::string::npos) {
        return false;
    }

    auto pos = colon + 1;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') {
        return false;
    }
    ++pos;

    std::string result;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') {
            value = std::move(result);
            return true;
        }

        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }

        if (pos >= json.size()) {
            return false;
        }

        const char escaped = json[pos++];
        switch (escaped) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: return false;
        }
    }

    return false;
}

std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::filesystem::current_path();
    }

    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

void setConnected(bool connected) {
    gConnected = connected;

    SetWindowTextW(gStatus, connected
        ? L"Server: connected to http://127.0.0.1:7842"
        : L"Server: disconnected");

    EnableWindow(gStartCore, connected ? FALSE : TRUE);
    EnableWindow(gRestartCore, TRUE);
    EnableWindow(gUpdate, connected ? TRUE : FALSE);
}

bool startCore() {
    if (gConnected) {
        appendHistory(L"[core] Core is already running.\r\n\r\n");
        return true;
    }

    const std::filesystem::path appDir = executableDirectory();
    const std::filesystem::path corePath = appDir / L"AI-Agent-LVK.exe";

    std::error_code ec;
    if (!std::filesystem::exists(corePath, ec)) {
        appendHistory(L"[core] AI-Agent-LVK.exe was not found next to the GUI.\r\n\r\n");
        return false;
    }

    std::wstring commandLine = L"\"" + corePath.wstring() + L"\" --headless";
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo{};

    const BOOL created = CreateProcessW(
        corePath.c_str(),
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
        appendHistory(
            L"[core] Failed to start AI-Agent-LVK.exe. Win32 error: " +
            std::to_wstring(GetLastError()) + L"\r\n\r\n");
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    appendHistory(L"[core] Started in background. Waiting for API...\r\n\r\n");
    return true;
}

void restartCore() {
    if (!gConnected) {
        appendHistory(L"[core] Core is not running; starting it instead.\r\n");
        startCore();
        return;
    }

    const HWND bridge = FindWindowW(kCoreBridgeClass, nullptr);
    if (bridge == nullptr) {
        appendHistory(L"[core] Could not find the core control bridge.\r\n\r\n");
        return;
    }

    if (!PostMessageW(bridge, WM_CLOSE, 0, 0)) {
        appendHistory(
            L"[core] Restart request failed. Win32 error: " +
            std::to_wstring(GetLastError()) + L"\r\n\r\n");
        return;
    }

    gRestartPending = true;
    appendHistory(L"[core] Restart requested. Waiting for shutdown...\r\n\r\n");
}

void refreshStatus() {
    const auto response = gApi.get("/api/v1/status");
    const bool connected =
        response.transportOk && response.statusCode >= 200 && response.statusCode < 300;

    setConnected(connected);

    if (!connected && gRestartPending) {
        gRestartPending = false;
        startCore();
    }
}

void executeCoreCommand(const std::wstring& input, bool echoCommand) {
    if (input.empty()) {
        return;
    }

    if (echoCommand) {
        appendHistory(L"> " + input + L"\r\n");
    }

    const std::string command = wideToUtf8(input);
    const std::string body = "{\"command\":\"" + jsonEscape(command) + "\"}";
    const auto response = gApi.postJson("/api/v1/command", body);

    if (!response.transportOk) {
        setConnected(false);
        appendHistory(L"[transport error] " + utf8ToWide(response.error) + L"\r\n\r\n");
        return;
    }

    setConnected(true);

    std::string message;
    if (!extractJsonString(response.body, "result", message)) {
        if (!extractJsonString(response.body, "error", message)) {
            message = response.body;
        }
    }

    appendHistory(utf8ToWide(message) + L"\r\n\r\n");
}

void sendCommand() {
    const std::wstring input = getText(gInput);
    if (input.empty()) {
        return;
    }

    SetWindowTextW(gInput, L"");
    executeCoreCommand(input, true);
}

void requestUpdate() {
    if (!gConnected) {
        appendHistory(L"[update] Core is not connected. Start Core first.\r\n\r\n");
        return;
    }

    executeCoreCommand(L"update", true);
}

void layoutControls(HWND window) {
    RECT client{};
    GetClientRect(window, &client);

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    MoveWindow(gStatus, 10, 10, width - 330, 24, TRUE);
    MoveWindow(gUpdate, width - 310, 8, 70, 26, TRUE);
    MoveWindow(gStartCore, width - 230, 8, 100, 26, TRUE);
    MoveWindow(gRestartCore, width - 120, 8, 110, 26, TRUE);
    MoveWindow(gHistory, 10, 40, width - 20, height - 100, TRUE);
    MoveWindow(gInput, 10, height - 50, width - 100, 26, TRUE);
    MoveWindow(gSend, width - 80, height - 50, 70, 26, TRUE);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        gStatus = CreateWindowExW(
            0, L"STATIC", L"Server: checking...",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusId)), nullptr, nullptr);

        gUpdate = CreateWindowExW(
            0, L"BUTTON", L"Update",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUpdateId)), nullptr, nullptr);

        gStartCore = CreateWindowExW(
            0, L"BUTTON", L"Start Core",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartCoreId)), nullptr, nullptr);

        gRestartCore = CreateWindowExW(
            0, L"BUTTON", L"Restart Core",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRestartCoreId)), nullptr, nullptr);

        gHistory = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHistoryId)), nullptr, nullptr);

        gInput = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInputId)), nullptr, nullptr);

        gSend = CreateWindowExW(
            0, L"BUTTON", L"Send",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSendId)), nullptr, nullptr);

        const HWND controls[] = {
            gStatus, gUpdate, gStartCore, gRestartCore, gHistory, gInput, gSend
        };
        for (const HWND control : controls) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }

        appendHistory(L"AI-Agent-LVK GUI v" + utf8ToWide(AI_AGENT_LVK_VERSION) + L"\r\n");
        appendHistory(L"Enter sends commands. Available now: status, version, ping, help, update\r\n\r\n");

        refreshStatus();
        SetTimer(window, kStatusTimerId, kStatusPollMs, nullptr);
        SetFocus(gInput);
        return 0;
    }

    case WM_SIZE:
        layoutControls(window);
        return 0;

    case WM_TIMER:
        if (wParam == kStatusTimerId) {
            refreshStatus();
            return 0;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == kSendId && HIWORD(wParam) == BN_CLICKED) {
            sendCommand();
            SetFocus(gInput);
            return 0;
        }

        if (LOWORD(wParam) == kUpdateId && HIWORD(wParam) == BN_CLICKED) {
            requestUpdate();
            SetFocus(gInput);
            return 0;
        }

        if (LOWORD(wParam) == kStartCoreId && HIWORD(wParam) == BN_CLICKED) {
            startCore();
            SetFocus(gInput);
            return 0;
        }

        if (LOWORD(wParam) == kRestartCoreId && HIWORD(wParam) == BN_CLICKED) {
            restartCore();
            SetFocus(gInput);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(window, kStatusTimerId);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const wchar_t kClassName[] = L"AI-Agent-LVK-GUI-Window";

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassW(&windowClass)) {
        return 1;
    }

    const HWND window = CreateWindowExW(
        0,
        kClassName,
        L"AI-Agent-LVK GUI",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        760,
        520,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!window) {
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.hwnd == gInput && message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
            sendCommand();
            SetFocus(gInput);
            continue;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
