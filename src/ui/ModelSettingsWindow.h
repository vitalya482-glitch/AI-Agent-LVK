#pragma once
#include <windows.h>
#include <string>
#include "config/ConfigManager.h"

namespace lvk::ui {
// Opens a modal native Win32 editor for the selected model profile.
// Returns true only when the user presses Save and all values validate.
bool showModelSettings(HWND parent, config::Profile& profile, std::string& error);
}
