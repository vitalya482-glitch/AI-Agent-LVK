#pragma once
#include <filesystem>
#include <string>

namespace lvk::process {
struct RunResult { bool started=false; unsigned long exitCode=1; std::string output; std::string error; };
RunResult runHidden(const std::filesystem::path& executable, const std::wstring& arguments, const std::filesystem::path& workingDirectory = {});
std::filesystem::path findOnPath(const std::wstring& executable);
}
