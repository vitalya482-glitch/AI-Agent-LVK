#pragma once

#include <windows.h>

#include <atomic>
#include <thread>

namespace lvk::update {

class UpdateCloseBridge {
public:
    UpdateCloseBridge() = default;
    ~UpdateCloseBridge();

    UpdateCloseBridge(const UpdateCloseBridge&) = delete;
    UpdateCloseBridge& operator=(const UpdateCloseBridge&) = delete;

    bool start();
    void stop();

private:
    void run();

    std::thread thread_;
    std::atomic<HWND> window_{nullptr};
    std::atomic<bool> running_{false};
};

} // namespace lvk::update
