#pragma once
#include <filesystem>
#include <mutex>
#include <string>
#include <functional>
namespace lvk::log { class LogManager { public: explicit LogManager(std::filesystem::path dir); void write(const std::string& line); void setListener(std::function<void(const std::string&)> listener); private: std::filesystem::path file_, llamaFile_; std::mutex mutex_; std::function<void(const std::string&)> listener_; }; }
