#pragma once
#include "models/ModelDownloader.h"
#include <windows.h>
#include <functional>
#include <mutex>
#include <thread>

namespace lvk::ui {
class DownloadWindow {
public:
    using Installed=std::function<void(config::InstalledModel,const std::filesystem::path&)>;
    DownloadWindow(HWND parent,Installed installed,std::function<void(const std::string&)> log,
        std::function<void(const std::filesystem::path&)> rememberFolder);
    ~DownloadWindow();
    void show(const std::filesystem::path& lastFolder);
    void cancelAndClose();
    bool running() const {return running_||pickerOpen_;}
private:
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
    void begin();
    void completed();
    void renderProgress();
    void details();
    HWND parent_{},window_{},model_{},file_{},size_{},folder_{},browse_{},download_{},progress_{},status_{},cancelButton_{};
    Installed installed_;
    std::function<void(const std::string&)> log_;
    std::function<void(const std::filesystem::path&)> rememberFolder_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    bool running_=false,closeRequested_=false,pickerOpen_=false;
    std::mutex mutex_;
    models::DownloadProgress latest_;
    models::DownloadResult result_;
    models::DownloadRequest request_;
};
}
