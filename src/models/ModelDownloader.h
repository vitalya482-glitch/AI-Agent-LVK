#pragma once
#include "models/ModelCatalog.h"
#include <atomic>
#include <functional>

namespace lvk::models {
enum class ExistingPolicy { Ask, UseExisting, Replace };
struct DownloadRequest {
    ModelCatalogEntry entry;
    std::filesystem::path directory;
    ExistingPolicy existing=ExistingPolicy::Ask;
    bool restartPartial=false;
};
struct DownloadProgress {
    std::uint64_t bytes{}, total{};
    double seconds{}, bytesPerSecond{};
    std::string phase;
};
enum class DownloadStatus { Complete, NeedExistingChoice, NeedPartialChoice, Cancelled, Failed };
struct DownloadResult {
    DownloadStatus status=DownloadStatus::Failed;
    std::filesystem::path path;
    std::string message;
};
// Blocking service intended exclusively for an owned background thread. Policy is
// separate from transport, allowing future Range/resume without changing the UI API.
DownloadResult downloadModel(const DownloadRequest&, const std::atomic<bool>& cancel,
    const std::function<void(const DownloadProgress&)>& progress);
}
