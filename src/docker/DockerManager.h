#pragma once
#include <filesystem>
#include <string>
#include <utility>
namespace lvk::docker {
class DockerManager {
public:
    explicit DockerManager(std::string image):image_(std::move(image)){}
    ~DockerManager();
    bool engineRunning() const;
    bool startDesktop(std::string& error) const;
    bool imageReady() const;
    bool ensureSandbox(const std::filesystem::path& workspace,std::string& error);
    bool removeSandbox(std::string& error);
    std::string runtimeSpec() const;
    std::wstring sandboxArguments(const std::filesystem::path& workspace) const;
    bool rebuild(const std::filesystem::path& folder,std::string& error) const;
private:
    std::string image_;
    std::string containerId_;
};
}
