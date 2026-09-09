#pragma once
#include <filesystem>
#include <string>
#include <utility>
namespace lvk::docker { class DockerManager { public: explicit DockerManager(std::string image):image_(std::move(image)){} bool engineRunning() const; bool imageReady() const; bool rebuild(const std::filesystem::path& folder,std::string& error) const; private: std::string image_; }; }
