#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace lvk::config {
struct Profile {
    std::string name;
    std::filesystem::path model;
    int context = 32768, parallel = 1, gpuLayers = 999, cpuMoe = 27;
    int topK = 20, batchSize = 512, ubatchSize = 253;
    double temperature = 0.3, topP = 0.95;
    double presencePenalty = 0.0, repeatPenalty = 1.0, frequencyPenalty = 0.0;
    std::string kvK = "q8_0", kvV = "q8_0";
    std::string flashAttention = "on";
    std::string tools = "all", toolsRuntime = "docker:ai-cpp-sandbox";

    // Speculative decoding. MTP controls are enabled in the GUI only for
    // profiles whose GGUF is known to contain compatible MTP heads.
    bool mtpSupported = false;
    std::string specType = "none";
    int specDraftNMax = 2;
};
struct Settings {
    std::string llamaCommand = "llama", host = "127.0.0.1";
    unsigned short port = 8080;
    std::filesystem::path workspace = LR"(G:\AI\workspace)";
    std::string dockerImage = "ai-cpp-sandbox";
    bool autoStartServer = false;
    std::string selectedProfile;
    std::vector<Profile> profiles;
};
class ConfigManager {
public:
    explicit ConfigManager(std::filesystem::path directory);
    bool load(std::string& error);
    bool save(std::string& error) const;
    const Settings& settings() const noexcept { return settings_; }
    Settings& settings() noexcept { return settings_; }
    const Profile* selectedProfile() const noexcept;
    std::filesystem::path path() const { return path_; }
private:
    std::filesystem::path path_;
    Settings settings_;
};
}
