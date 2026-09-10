#include "UpdateCloseBridge.h"

#include <chrono>
#include <cstdio>

namespace lvk::update {
namespace {

constexpr wchar_t kWindowClassName[] = L"AI_AGENT_LVK_UPDATE_BRIDGE";
constexpr UINT kStopMessage = WM_APP + 42;

LRESULT CALLBACK bridgeWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CLOSE:
        // LVK-Updater sends WM_CLOSE only after the user has confirmed the
        // update and the downloaded package has passed verification.
        // Use the main shutdown path: cancel/join downloads, persist registration,
        // and stop only our owned llama process before the updater replaces files.
        EnumWindows([](HWND window,LPARAM)->BOOL{
            DWORD pid{};GetWindowThreadProcessId(window,&pid);wchar_t name[128]{};GetClassNameW(window,name,128);
            if(pid==GetCurrentProcessId()&&wcscmp(name,L"AI-Agent-LVK-Window")==0){PostMessageW(window,WM_CLOSE,0,0);return FALSE;}
            return TRUE;
        },0);
        return 0;

    case kStopMessage:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace

UpdateCloseBridge::~UpdateCloseBridge() {
    stop();
}

bool UpdateCloseBridge::start() {
    if (running_.exchange(true)) {
        return true;
    }

    thread_ = std::thread(&UpdateCloseBridge::run, this);

    for (int i = 0; i < 100; ++i) {
        if (window_.load() != nullptr) {
            return true;
        }
        if (!running_.load()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return window_.load() != nullptr;
}

void UpdateCloseBridge::stop() {
    const bool wasRunning = running_.exchange(false);

    if (wasRunning) {
        if (const HWND hwnd = window_.load(); hwnd != nullptr) {
            PostMessageW(hwnd, kStopMessage, 0, 0);
        }
    }

    // Always join a joinable worker, including the case where the worker
    // failed before creating its hidden window and already set running_=false.
    if (thread_.joinable()) {
        thread_.join();
    }

    window_.store(nullptr);
}

void UpdateCloseBridge::run() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = bridgeWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClassName;

    if (RegisterClassW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        running_.store(false);
        return;
    }

    const HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kWindowClassName,
        L"AI-Agent-LVK Update Bridge",
        WS_OVERLAPPED,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (hwnd == nullptr) {
        running_.store(false);
        return;
    }

    // Do not call ShowWindow(). The window only exists so LVK-Updater can
    // discover this process with EnumWindows and request a graceful shutdown.
    window_.store(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    window_.store(nullptr);
    running_.store(false);
}

} // namespace lvk::update
