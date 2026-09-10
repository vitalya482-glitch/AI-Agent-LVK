#pragma once
#include <windows.h>
#include <filesystem>
#include <optional>

namespace lvk::ui {
std::optional<std::filesystem::path> pickModelPath(HWND owner,bool folder);
}
