#pragma once

#include <functional>
#include <string>
#include <windows.h>

namespace lvk::ui {

class DockerSettingsWindow {
public:
    using Action = std::function<void(HWND)>;
    using Toggle = std::function<void(HWND, bool)>;

    DockerSettingsWindow(HWND parent, bool enabled, Toggle onToggle, Action onConnect,
                         Action onDisconnect, Action onRebuild, Action onOpenWorkspace,
                         Action onChangeWorkspace, Action onRefresh);
    ~DockerSettingsWindow();

    DockerSettingsWindow(const DockerSettingsWindow&) = delete;
    DockerSettingsWindow& operator=(const DockerSettingsWindow&) = delete;

    void show();
    void close();
    void setStatus(const std::string& status);
    HWND handle() const noexcept { return window_; }

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    static bool ensureClass();
    void createControls();
    void layout();
    void updateEnabledState();

    HWND parent_{};
    HWND window_{};
    HWND enabled_{};
    HWND status_{};
    bool enabledValue_ = false;
    Toggle onToggle_;
    Action onConnect_, onDisconnect_, onRebuild_, onOpenWorkspace_, onChangeWorkspace_, onRefresh_;
};

}
