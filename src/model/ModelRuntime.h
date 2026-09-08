#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace lvk::model {

struct ModelConfig {
    int contextSize = 4096;
    int threadCount = 0;
    int gpuLayers = 0;
    int batchSize = 512;
    bool kvCacheOnGpu = false;
    int flashAttention = -1; // -1 auto, 0 disabled, 1 enabled
    bool useMmap = true;
    bool useMlock = false;
};
struct ModelResult { bool ok = false; std::string message; };
struct ModelStatus {
    bool loaded = false;
    bool gpuAvailable = false;
    std::string path;
    std::string description;
    ModelConfig config;
    std::uint64_t modelSizeBytes = 0;
    std::uint64_t parameterCount = 0;
    std::uint64_t cpuWeightBytesEstimate = 0;
    std::uint64_t gpuWeightBytesEstimate = 0;
    int modelLayers = 0;
    int gpuLayersLoaded = 0;
    int contextTokensUsed = 0;
    bool generationActive = false;
};

class ModelRuntime {
public:
    ModelRuntime();
    ~ModelRuntime();
    ModelRuntime(const ModelRuntime&) = delete;
    ModelResult load(const std::string& path);
    ModelResult configure(ModelConfig config);
    ModelResult chat(const std::string& message, std::string& response, int maxTokens = 256);
    void unload();
    [[nodiscard]] ModelStatus status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace lvk::model
