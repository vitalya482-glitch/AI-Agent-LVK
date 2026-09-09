#include "memory/MemoryMonitor.h"

#include <limits>
#include <vector>
#include <psapi.h>

namespace lvk::memory {
namespace {

using NvmlReturn = int;
using NvmlDevice = void*;
constexpr NvmlReturn kNvmlSuccess = 0;
constexpr NvmlReturn kNvmlInsufficientSize = 7;
constexpr auto kNvmlValueNotAvailable = std::numeric_limits<unsigned long long>::max();

struct NvmlMemoryInfo {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

struct NvmlProcessInfoV2 {
    unsigned int pid;
    unsigned long long usedGpuMemory;
};

struct NvmlProcessInfoV3 {
    unsigned int pid;
    unsigned long long usedGpuMemory;
    unsigned int gpuInstanceId;
    unsigned int computeInstanceId;
};

using NvmlInit = NvmlReturn (*)();
using NvmlShutdown = NvmlReturn (*)();
using NvmlDeviceGetHandleByIndex = NvmlReturn (*)(unsigned int, NvmlDevice*);
using NvmlDeviceGetMemoryInfo = NvmlReturn (*)(NvmlDevice, NvmlMemoryInfo*);
using NvmlDeviceGetComputeProcessesV2 = NvmlReturn (*)(NvmlDevice, unsigned int*, NvmlProcessInfoV2*);
using NvmlDeviceGetComputeProcessesV3 = NvmlReturn (*)(NvmlDevice, unsigned int*, NvmlProcessInfoV3*);

template <typename Function>
Function loadFunction(HMODULE module, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

void collectNvidiaMemory(Snapshot& result, unsigned long processId) {
    const HMODULE module = LoadLibraryW(L"nvml.dll");
    if (!module) {
        result.vramMessage = "VRAM unavailable: NVIDIA NVML library was not found.";
        return;
    }

    const auto init = loadFunction<NvmlInit>(module, "nvmlInit_v2");
    const auto shutdown = loadFunction<NvmlShutdown>(module, "nvmlShutdown");
    const auto getDevice = loadFunction<NvmlDeviceGetHandleByIndex>(module, "nvmlDeviceGetHandleByIndex_v2");
    const auto getMemory = loadFunction<NvmlDeviceGetMemoryInfo>(module, "nvmlDeviceGetMemoryInfo");
    const auto getProcessesV3 = loadFunction<NvmlDeviceGetComputeProcessesV3>(module, "nvmlDeviceGetComputeRunningProcesses_v3");
    const auto getProcessesV2 = loadFunction<NvmlDeviceGetComputeProcessesV2>(module, "nvmlDeviceGetComputeRunningProcesses_v2");

    if (!init || !shutdown || !getDevice || !getMemory) {
        result.vramMessage = "VRAM unavailable: NVML does not expose the required functions.";
        FreeLibrary(module);
        return;
    }

    if (init() != kNvmlSuccess) {
        result.vramMessage = "VRAM unavailable: NVML initialization failed.";
        FreeLibrary(module);
        return;
    }

    NvmlDevice device = nullptr;
    NvmlMemoryInfo memory{};
    if (getDevice(0, &device) != kNvmlSuccess || getMemory(device, &memory) != kNvmlSuccess) {
        result.vramMessage = "VRAM unavailable: NVIDIA device memory could not be queried.";
        shutdown();
        FreeLibrary(module);
        return;
    }

    result.vramAvailable = true;
    result.totalVram = memory.total;
    result.usedVram = memory.used;
    result.availableVram = memory.free;

    if (processId != 0 && (getProcessesV3 || getProcessesV2)) {
        unsigned int count = 0;
        NvmlReturn status = getProcessesV3
            ? getProcessesV3(device, &count, nullptr)
            : getProcessesV2(device, &count, nullptr);
        if (status == kNvmlInsufficientSize && count != 0) {
            if (getProcessesV3) {
                std::vector<NvmlProcessInfoV3> processes(count);
                status = getProcessesV3(device, &count, processes.data());
                if (status == kNvmlSuccess) {
                    for (const auto& process : processes) {
                        if (process.pid == processId && process.usedGpuMemory != kNvmlValueNotAvailable) {
                            result.aiVram = process.usedGpuMemory;
                            break;
                        }
                    }
                }
            } else {
                std::vector<NvmlProcessInfoV2> processes(count);
                status = getProcessesV2(device, &count, processes.data());
                if (status == kNvmlSuccess) {
                    for (const auto& process : processes) {
                        if (process.pid == processId && process.usedGpuMemory != kNvmlValueNotAvailable) {
                            result.aiVram = process.usedGpuMemory;
                            break;
                        }
                    }
                }
            }
        }
    }

    shutdown();
    FreeLibrary(module);
}

} // namespace

Snapshot collect(HANDLE processHandle, unsigned long processId) {
    Snapshot result;

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        result.systemRamAvailable = true;
        result.totalRam = memory.ullTotalPhys;
        result.availableRam = memory.ullAvailPhys;
        result.usedRam = result.totalRam >= result.availableRam ? result.totalRam - result.availableRam : 0;
    }

    if (processHandle != nullptr) {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(processHandle, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
            result.aiRam = counters.WorkingSetSize;
        }
    }

    collectNvidiaMemory(result, processId);
    return result;
}

} // namespace lvk::memory
