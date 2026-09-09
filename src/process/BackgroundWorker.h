#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace lvk::process {
class BackgroundWorker {
public:
    BackgroundWorker();
    ~BackgroundWorker();
    BackgroundWorker(const BackgroundWorker&) = delete;
    BackgroundWorker& operator=(const BackgroundWorker&) = delete;
    void submit(std::function<void()> job);
private:
    void run();
    std::mutex mutex_;
    std::condition_variable wake_;
    std::queue<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::thread thread_;
};
}
