#pragma once

#include <string>

namespace lvk::update {

struct LaunchResult {
    bool ok = false;
    std::string message;
};

class UpdateManager {
public:
    static LaunchResult launchCheck();
};

} // namespace lvk::update
