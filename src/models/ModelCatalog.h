#pragma once
#include "config/ConfigManager.h"
#include <cstdint>
#include <vector>

namespace lvk::models {
struct ModelCatalogEntry {
    std::string id, displayName, family, quant, filename, downloadUrl;
    std::uint64_t expectedSize{};
    std::string sha256;
    int recommendedContext=8192, maxContext=8192;
    bool mtpSupported=false;
    std::string mtpDownloadUrl;
};
const std::vector<ModelCatalogEntry>& catalog();
config::InstalledModel fromCatalog(const ModelCatalogEntry&, const std::filesystem::path&);
config::InstalledModel fromExisting(const std::filesystem::path&);
// Filesystem validation runs on a worker, not the message loop.
std::filesystem::path validateGguf(const std::filesystem::path&);
std::string newModelId();
// Reuse a registration for the same path without resetting its tuning.
void registerModel(config::Settings&, config::InstalledModel);
}
