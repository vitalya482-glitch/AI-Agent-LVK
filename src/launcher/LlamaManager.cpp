#include "launcher/LlamaManager.h"
#include "port/PortChecker.h"
#include <filesystem>

namespace lvk::launcher {
namespace {
std::wstring q(const std::filesystem::path& p) { return L"\"" + p.wstring() + L"\""; }
std::wstring w(const std::string& s) { return {s.begin(), s.end()}; }
}

std::wstring LlamaManager::commandLine(const config::Settings& s, const config::Profile& p) const {
    std::wstring line = L"serve -m " + q(p.model)
        + L" -c " + std::to_wstring(p.context)
        + L" -np " + std::to_wstring(p.parallel)
        + L" -ngl " + std::to_wstring(p.gpuLayers)
        + L" -ncmoe " + std::to_wstring(p.cpuMoe)
        + L" --temp " + std::to_wstring(p.temperature)
        + L" --top-k " + std::to_wstring(p.topK)
        + L" --top-p " + std::to_wstring(p.topP)
        + L" --presence-penalty " + std::to_wstring(p.presencePenalty)
        + L" --repeat-penalty " + std::to_wstring(p.repeatPenalty)
        + L" --frequency-penalty " + std::to_wstring(p.frequencyPenalty)
        + L" --batch-size " + std::to_wstring(p.batchSize)
        + L" --ubatch-size " + std::to_wstring(p.ubatchSize)
        + L" -ctk " + w(p.kvK)
        + L" -ctv " + w(p.kvV)
        + L" -fa " + (p.flashAttention ? L"on" : L"off");

    // Never request MTP from a profile that is not explicitly marked as MTP-capable.
    if (p.mtpSupported && p.specType != "none") {
        line += L" --spec-type " + w(p.specType)
            + L" --spec-draft-n-max " + std::to_wstring(p.specDraftNMax);
    }

    line += L" --tools " + w(p.tools)
        + L" --tools-runtime " + w(p.toolsRuntime)
        + L" --host " + w(s.host)
        + L" --port " + std::to_wstring(s.port);
    return line;
}

bool LlamaManager::start(const config::Settings& s, const config::Profile& p, std::string& e) {
    if (running()) { e = "llama server is already running."; return false; }
    if (!std::filesystem::is_regular_file(p.model)) { e = "GGUF model file not found."; return false; }
    const std::wstring exe = w(s.llamaCommand);
    if (!process_.start(exe, commandLine(s, p), {}, [this](const std::string& x) { log_.write(x); })) {
        e = "Could not start llama serve. Check llama_command and its permissions.";
        return false;
    }
    return true;
}
bool LlamaManager::stop() { return process_.stop(); }
bool LlamaManager::running() const { return process_.running(); }
}
