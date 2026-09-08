#include "ChatWindow.h"

#include "ApiClient.h"
#include "core/AppConfig.h"

#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace lvk::gui {
namespace {

constexpr int kHistoryId = 2001;
constexpr int kInputId = 2002;
constexpr int kSendId = 2003;
constexpr UINT kChatCompleted = WM_APP + 20;

struct ChatResult { std::wstring text; };

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return L"[invalid UTF-8]";
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1);
    GetWindowTextW(control, buffer.data(), length + 1);
    return {buffer.data(), static_cast<size_t>(length)};
}

std::string jsonEscape(std::string_view value) {
    std::string result;
    for (const char ch : value) {
        switch (ch) { case '\\': result += "\\\\"; break; case '"': result += "\\\""; break; case '\n': result += "\\n"; break; case '\r': result += "\\r"; break; case '\t': result += "\\t"; break; default: result += ch; }
    }
    return result;
}

bool extractJsonString(const std::string& json, std::string_view field, std::string& value) {
    const auto fieldPos = json.find("\"" + std::string(field) + "\"");
    if (fieldPos == std::string::npos) return false;
    auto pos = json.find(':', fieldPos + field.size() + 2);
    if (pos == std::string::npos) return false;
    while (++pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {}
    if (pos >= json.size() || json[pos++] != '"') return false;
    std::string result;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') { value = std::move(result); return true; }
        if (ch != '\\') { result += ch; continue; }
        if (pos >= json.size()) return false;
        switch (json[pos++]) { case '"': result += '"'; break; case '\\': result += '\\'; break; case 'n': result += '\n'; break; case 'r': result += '\r'; break; case 't': result += '\t'; break; default: return false; }
    }
    return false;
}

void append(HWND history, const std::wstring& line) {
    const auto length = SendMessageW(history, WM_GETTEXTLENGTH, 0, 0);
    SendMessageW(history, EM_SETSEL, length, length);
    SendMessageW(history, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

void layout(HWND window, HWND history, HWND input, HWND send) {
    RECT client{}; GetClientRect(window, &client);
    MoveWindow(history, 14, 14, client.right - 28, client.bottom - 82, TRUE);
    MoveWindow(input, 14, client.bottom - 54, client.right - 116, 28, TRUE);
    MoveWindow(send, client.right - 92, client.bottom - 54, 78, 28, TRUE);
}

LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    HWND history = reinterpret_cast<HWND>(GetPropW(window, L"history"));
    HWND input = reinterpret_cast<HWND>(GetPropW(window, L"input"));
    switch (message) {
    case WM_CREATE: {
        const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        history = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kHistoryId), nullptr, nullptr);
        input = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kInputId), nullptr, nullptr);
        const HWND send = CreateWindowExW(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kSendId), nullptr, nullptr);
        for (const auto control : {history, input, send}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetPropW(window, L"history", history); SetPropW(window, L"input", input); SetPropW(window, L"send", send);
        append(history, L"Chat is ready. Load a GGUF model in the main window first.\r\n\r\n");
        layout(window, history, input, send); SetFocus(input); return 0;
    }
    case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {520, 440}; return 0;
    case WM_SIZE: layout(window, history, input, reinterpret_cast<HWND>(GetPropW(window, L"send"))); return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == kSendId && HIWORD(wParam) == BN_CLICKED) {
            const auto messageText = text(input); if (messageText.empty()) return 0;
            SetWindowTextW(input, L""); EnableWindow(input, FALSE); EnableWindow(reinterpret_cast<HWND>(GetPropW(window, L"send")), FALSE);
            append(history, L"You: " + messageText + L"\r\nAI: generating...\r\n");
            std::thread([window, messageText] {
                ApiClient api(core::kDefaultApiHost, core::kDefaultApiPort);
                const auto response = api.postJson("/api/v1/chat", "{\"message\":\"" + jsonEscape(wideToUtf8(messageText)) + "\"}");
                std::string answer;
                if (!response.transportOk) answer = "Connection failed: " + response.error;
                else if (!extractJsonString(response.body, "result", answer) && !extractJsonString(response.body, "error", answer)) answer = response.body;
                auto* result = new ChatResult{utf8ToWide(answer)};
                if (!PostMessageW(window, kChatCompleted, 0, reinterpret_cast<LPARAM>(result))) delete result;
            }).detach();
            return 0;
        }
        break;
    case kChatCompleted: {
        std::unique_ptr<ChatResult> result(reinterpret_cast<ChatResult*>(lParam));
        append(history, L"AI: " + result->text + L"\r\n\r\n");
        EnableWindow(input, TRUE); EnableWindow(reinterpret_cast<HWND>(GetPropW(window, L"send")), TRUE); SetFocus(input); return 0;
    }
    case WM_DESTROY: RemovePropW(window, L"history"); RemovePropW(window, L"input"); RemovePropW(window, L"send"); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

void openChatWindow(HINSTANCE instance, HWND owner) {
    constexpr wchar_t className[] = L"AI-Agent-LVK-Chat-Window";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW windowClass{}; windowClass.lpfnWndProc = proc; windowClass.hInstance = instance; windowClass.lpszClassName = className; windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW); windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        registered = RegisterClassW(&windowClass) != 0;
    }
    const HWND window = CreateWindowExW(0, className, L"AI-Agent-LVK Chat", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 650, owner, nullptr, instance, nullptr);
    if (window) { ShowWindow(window, SW_SHOW); UpdateWindow(window); }
}

} // namespace lvk::gui
