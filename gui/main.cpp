#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ApiClient.h"
#include "core/AppConfig.h"

#include <cctype>
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

HWND gStatus = nullptr;
HWND gHistory = nullptr;
HWND gInput = nullptr;
HWND gSend = nullptr;

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

void setConnected(bool connected) {
    SetWindowTextW(gStatus, connected
        ? L"Server: connected to http://127.0.0.1:7842"
        : L"Server: disconnected (start AI-Agent-LVK.exe first)");
}

void refreshStatus() {
    const auto response = gApi.get("/api/v1/status");
    setConnected(response.transportOk && response.statusCode >= 200 && response.statusCode < 300);
}

void sendCommand() {
    const std::wstring input = getText(gInput);
    if (input.empty()) {
        return;
    }

    appendHistory(L"> " + input + L"\r\n");
    SetWindowTextW(gInput, L"");

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

void layoutControls(HWND window) {
    RECT client{};
    GetClientRect(window, &client);

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    MoveWindow(gStatus, 10, 10, width - 20, 20, TRUE);
    MoveWindow(gHistory, 10, 35, width - 20, height - 95, TRUE);
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

        const HWND controls[] = {gStatus, gHistory, gInput, gSend};
        for (const HWND control : controls) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }

        appendHistory(L"AI-Agent-LVK GUI v" + utf8ToWide(AI_AGENT_LVK_VERSION) + L"\r\n");
        appendHistory(L"Type a core command such as: status, version, ping, help\r\n\r\n");
        refreshStatus();
        SetFocus(gInput);
        return 0;
    }

    case WM_SIZE:
        layoutControls(window);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == kSendId && HIWORD(wParam) == BN_CLICKED) {
            sendCommand();
            SetFocus(gInput);
            return 0;
        }
        break;

    case WM_DESTROY:
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
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
