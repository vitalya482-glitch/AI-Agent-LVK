#include "launcher/LlamaManager.h"
#include "port/PortChecker.h"
#include "process/ProcessRunner.h"
#include "util/Text.h"
#include <filesystem>

namespace lvk::launcher {
namespace {
std::wstring q(const std::filesystem::path& p) { return util::quote(p.wstring()); }
std::wstring w(const std::string& s) { return util::wide(s); }
std::wstring arg(const std::string& s) { return util::quote(w(s)); }
}

std::wstring LlamaManager::commandLine(const config::Settings& s, const config::Profile& p) const {
    const auto executable=std::filesystem::path(w(s.llamaCommand)).filename().wstring();
    const bool directServer=_wcsicmp(executable.c_str(),L"llama-server.exe")==0||_wcsicmp(executable.c_str(),L"llama-server")==0;
    std::wstring line = std::wstring(directServer?L"-m ":L"serve -m ") + q(p.model)
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
        + L" -ctk " + arg(p.kvK)
        + L" -ctv " + arg(p.kvV)
        + L" --flash-attn " + arg(p.flashAttention);

    // Never request MTP from a profile that is not explicitly marked as MTP-capable.
    if (p.mtpSupported && p.mtpFileAvailable && !p.mtpModelPath.empty() && p.specType != "none") {
        line += L" --model-draft " + q(p.mtpModelPath) + L" --spec-type " + arg(p.specType)
            + L" --spec-draft-n-max " + std::to_wstring(p.specDraftNMax);
    }

    line += L" --tools " + arg(p.tools)
        + L" --tools-runtime " + arg(p.toolsRuntime)
        + L" --host " + arg(s.host)
        + L" --port " + std::to_wstring(s.port);
    return line;
}

bool LlamaManager::start(const config::Settings& s, const config::Profile& p, std::string& e) {
    if (running()) { e = "llama server is already running."; return false; }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p.model,ec)) { e = "Model file not found: "+util::pathText(p.model); return false; }
    const auto exe=process::findOnPath(w(s.llamaCommand));
    if(exe.empty()){e="llama executable not found: "+s.llamaCommand;return false;}
    auto checked=p;checked.mtpFileAvailable=!p.mtpModelPath.empty()&&std::filesystem::is_regular_file(p.mtpModelPath,ec);
    log_.write("Starting: "+util::utf8(util::quote(exe.wstring())+L" "+commandLine(s,checked)));
    if (!process_.start(exe.wstring(), commandLine(s, checked), {}, [this](const std::string& x) { log_.write(x); })) {
        e = "Could not start llama serve. Check llama_command and its permissions.";
        return false;
    }
    return true;
}
bool LlamaManager::stop() { return process_.stop(); }
bool LlamaManager::running() const { return process_.running(); }
}
