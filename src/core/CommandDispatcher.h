#pragma once

#include <string>

namespace lvk::core {

struct CommandResult {
    bool ok = false;
    std::string output;
};

class CommandDispatcher {
public:
    explicit CommandDispatcher(std::string version);

    CommandResult execute(const std::string& command) const;
    const std::string& version() const noexcept;

    static std::string normalizeCommand(std::string value);

private:
    std::string version_;
};

} // namespace lvk::core
