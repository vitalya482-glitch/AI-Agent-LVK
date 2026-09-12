#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace lvk::config {
struct Profile {
    std::string name;
    std::filesystem::path model;
    std::string id, family, quant;
    std::filesystem::path mtpModelPath;
    bool missing = false, mtpFileAvailable = false; // Background filesystem status; not serialized.
    int context = 32768, maxContext = 262144, parallel = 1, gpuLayers = 999, cpuMoe = 27;
    int topK = 20, batchSize = 512, ubatchSize = 253;
    double temperature = 0.3, topP = 0.95;
    double presencePenalty = 0.0, repeatPenalty = 1.0, frequencyPenalty = 0.0;
    std::string kvK = "q8_0", kvV = "q8_0";
    std::string flashAttention = "on";
    std::string tools = "all", toolsRuntime = "docker:ai-cpp-sandbox";
    // Web UI agentic loop limit: 0 = do not override Web UI, -1 = Infinity.
    int agentTurnLimit = 0;

    // Highest context size this profile is intended to expose in the GUI.
    // The Model Settings slider uses discrete power-of-two-ish positions and
    // hides values above this profile-specific capability limit.
    int maxContextCapability() const noexcept { return maxContext; }

    // Speculative decoding. MTP controls are enabled in the GUI only for
    // explicit capability metadata AND a separate, available MTP draft file.
    bool mtpSupported = false;
    std::string specType = "none";
    int specDraftNMax = 2;
};
struct Settings {
    std::string llamaCommand = "llama", host = "127.0.0.1";
    unsigned short port = 8080;
    std::filesystem::path workspace;
    std::string dockerImage = "ai-cpp-sandbox";
    bool dockerEnabled = false;
    bool autoStartServer = false;
    std::string selectedProfile;
    std::vector<Profile> profiles;
    std::filesystem::path lastModelDownloadDirectory;
    std::filesystem::path lastModelBrowseDirectory;
};
using InstalledModel = Profile; // Existing tuning UI operates on an installed model's profile.
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
