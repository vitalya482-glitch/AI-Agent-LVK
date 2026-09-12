#include "ui/DockerSettingsWindow.h"
#include "util/Text.h"

#include <algorithm>

namespace lvk::ui {
namespace {
constexpr wchar_t kClassName[] = L"AI-Agent-LVK-DockerSettings";
constexpr int kEnabled = 9101;
constexpr int kConnect = 9102;
constexpr int kDisconnect = 9103;
constexpr int kRebuild = 9104;
constexpr int kOpenWorkspace = 9105;
constexpr int kChangeWorkspace = 9106;
constexpr int kRefresh = 9107;
constexpr int kClose = 9108;
}

DockerSettingsWindow::DockerSettingsWindow(HWND parent, bool enabled, Toggle onToggle,
    Action onConnect, Action onDisconnect, Action onRebuild, Action onOpenWorkspace,
    Action onChangeWorkspace, Action onRefresh)
    : parent_(parent), enabledValue_(enabled), onToggle_(std::move(onToggle)),
      onConnect_(std::move(onConnect)), onDisconnect_(std::move(onDisconnect)),
      onRebuild_(std::move(onRebuild)), onOpenWorkspace_(std::move(onOpenWorkspace)),
      onChangeWorkspace_(std::move(onChangeWorkspace)), onRefresh_(std::move(onRefresh)) {}

DockerSettingsWindow::~DockerSettingsWindow() { close(); }

bool DockerSettingsWindow::ensureClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSW klass{};
    klass.lpfnWndProc = &DockerSettingsWindow::windowProc;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = kClassName;
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    klass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    registered = RegisterClassW(&klass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return registered;
}

void DockerSettingsWindow::show() {
    if (window_) {
        ShowWindow(window_, SW_SHOWNORMAL);
        SetForegroundWindow(window_);
        return;
    }
    if (!ensureClass()) return;
    window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, L"Docker Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 690, 410, parent_, nullptr,
        GetModuleHandleW(nullptr), this);
    if (!window_) return;
    RECT parentRect{}, windowRect{};
    if (parent_ && GetWindowRect(parent_, &parentRect) && GetWindowRect(window_, &windowRect)) {
        const int width = windowRect.right - windowRect.left;
        const int height = windowRect.bottom - windowRect.top;
        const int x = std::max(10, static_cast<int>(parentRect.left + ((parentRect.right - parentRect.left) - width) / 2));
        const int y = std::max(10, static_cast<int>(parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2));
        SetWindowPos(window_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
}

void DockerSettingsWindow::close() {
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
}

void DockerSettingsWindow::setStatus(const std::string& status) {
    if (!status_ || !IsWindow(status_)) return;
    const auto value = util::wide(status);
    SetWindowTextW(status_, value.c_str());
}

void DockerSettingsWindow::createControls() {
    enabled_ = CreateWindowW(L"BUTTON", L"Enable Docker sandbox",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 20, 260, 26, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEnabled)), nullptr, nullptr);
    SendMessageW(enabled_, BM_SETCHECK, enabledValue_ ? BST_CHECKED : BST_UNCHECKED, 0);
    CreateWindowW(L"STATIC", L"Docker is optional. Start AI can run llama.cpp without it.",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 300, 22, 350, 22, window_, nullptr, nullptr, nullptr);
    status_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Checking Docker status...",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
        20, 62, 630, 145, window_, nullptr, nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Connect / Start Docker", WS_CHILD | WS_VISIBLE,
        20, 225, 190, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kConnect)), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Disconnect sandbox", WS_CHILD | WS_VISIBLE,
        220, 225, 160, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDisconnect)), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Rebuild Sandbox", WS_CHILD | WS_VISIBLE,
        390, 225, 150, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRebuild)), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Refresh status", WS_CHILD | WS_VISIBLE,
        550, 225, 100, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRefresh)), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Open Workspace", WS_CHILD | WS_VISIBLE,
        20, 270, 150, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenWorkspace)), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Change Workspace", WS_CHILD | WS_VISIBLE,
        180, 270, 160, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChangeWorkspace)), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE,
        565, 335, 85, 30, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClose)), nullptr, nullptr);
    updateEnabledState();
}

void DockerSettingsWindow::layout() {
    RECT rect{};
    GetClientRect(window_, &rect);
    const int width = std::max(500L, rect.right);
    MoveWindow(status_, 20, 62, width - 40, 145, TRUE);
}

void DockerSettingsWindow::updateEnabledState() {
    if (!enabled_) return;
    enabledValue_ = SendMessageW(enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

LRESULT CALLBACK DockerSettingsWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<DockerSettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<DockerSettingsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }
    if (!self) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE:
        self->createControls();
        return 0;
    case WM_SIZE:
        self->layout();
        return 0;
    case WM_COMMAND: {
        const auto id = LOWORD(wParam);
        if (id == kEnabled && HIWORD(wParam) == BN_CLICKED) {
            self->updateEnabledState();
            if (self->onToggle_) self->onToggle_(window, self->enabledValue_);
            return 0;
        }
        if (HIWORD(wParam) != BN_CLICKED) break;
        if (id == kConnect && self->onConnect_) self->onConnect_(window);
        else if (id == kDisconnect && self->onDisconnect_) self->onDisconnect_(window);
        else if (id == kRebuild && self->onRebuild_) self->onRebuild_(window);
        else if (id == kOpenWorkspace && self->onOpenWorkspace_) self->onOpenWorkspace_(window);
        else if (id == kChangeWorkspace && self->onChangeWorkspace_) self->onChangeWorkspace_(window);
        else if (id == kRefresh && self->onRefresh_) self->onRefresh_(window);
        else if (id == kClose) DestroyWindow(window);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        self->window_ = nullptr;
        self->enabled_ = nullptr;
        self->status_ = nullptr;
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}
