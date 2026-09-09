#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <windows.h>

namespace lvk::memory {

struct Snapshot {
    bool systemRamAvailable = false;
    std::uint64_t totalRam = 0;
    std::uint64_t usedRam = 0;
    std::uint64_t availableRam = 0;

    bool vramAvailable = false;
    std::uint64_t totalVram = 0;
    std::uint64_t usedVram = 0;
    std::uint64_t availableVram = 0;
    std::string vramMessage;

    std::optional<std::uint64_t> aiRam;
    std::optional<std::uint64_t> aiVram;
};

Snapshot collect(HANDLE processHandle, unsigned long processId);

} // namespace lvk::memory
