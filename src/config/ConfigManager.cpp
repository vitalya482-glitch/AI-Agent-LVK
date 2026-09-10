#include "config/ConfigManager.h"
#include <fstream>
#include <regex>
#include <sstream>
#include <string_view>
#include <utility>

namespace lvk::config {
namespace {
std::string read(const std::string& s, const char* key, const std::string& d) { std::regex r("\\\""+std::string(key)+"\\\"\\s*:\\s*\\\"([^\\\"]*)\\\""); std::smatch m; return std::regex_search(s,m,r)?m[1].str():d; }
int number(const std::string& s, const char* key, int d) { std::regex r("\\\""+std::string(key)+"\\\"\\s*:\\s*(-?[0-9]+)"); std::smatch m; return std::regex_search(s,m,r)?std::stoi(m[1].str()):d; }
double decimal(const std::string& s, const char* key, double d) { std::regex r("\\\""+std::string(key)+"\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)"); std::smatch m; return std::regex_search(s,m,r)?std::stod(m[1].str()):d; }
bool flag(const std::string& s, const char* key, bool d) { std::regex r("\\\""+std::string(key)+"\\\"\\s*:\\s*(true|false)"); std::smatch m; return std::regex_search(s,m,r)?m[1].str()=="true":d; }
std::string json(const std::string& s) { std::string r; for(char c:s) { if(c=='\\'||c=='\"') r+='\\'; if(c=='\n') r+="\\n"; else r+=c; } return r; }
}
ConfigManager::ConfigManager(std::filesystem::path directory):path_(std::move(directory)/L"config.json") {}
bool migrateDefaultQwenContext(std::string& text, std::string& error, const std::filesystem::path& path) {
    constexpr std::string_view profileName = "Qwen3-Coder-30B-A3B";
    const auto name = text.find(profileName);
    if (name == std::string::npos) return false;
    const auto begin = text.rfind('{', name);
    const auto end = text.find('}', name);
    if (begin == std::string::npos || end == std::string::npos || end <= begin) return false;
    const auto key = text.find("\"context\"", begin);
    if (key == std::string::npos || key >= end) return false;
    const auto colon = text.find(':', key);
    if (colon == std::string::npos || colon >= end) return false;
    const auto value = text.find_first_of("0123456789", colon + 1);
    if (value == std::string::npos || value >= end) return false;
    const bool old8192 = text.compare(value, 4, "8192") == 0 && (value + 4 >= end || text[value + 4] < '0' || text[value + 4] > '9');
    const bool old16384 = text.compare(value, 5, "16384") == 0 && (value + 5 >= end || text[value + 5] < '0' || text[value + 5] > '9');
    if (!old8192 && !old16384) return false;
    text.replace(value, old8192 ? 4 : 5, "32768");
    std::ofstream out(path, std::ios::trunc);
    if (!out) { error = "Cannot migrate context in " + path.string(); return false; }
    out << text;
    if (!out) { error = "Cannot write migrated context to " + path.string(); return false; }
    return true;
}
bool ConfigManager::load(std::string& error) {
    if(!std::filesystem::exists(path_)) { settings_.profiles.push_back({"Qwen3-Coder-30B-A3B", LR"(G:\AI\models\Qwen3-Coder-30B-A3B\Qwen3-Coder-30B-A3B-Instruct-Q4_K_M.gguf)"}); settings_.selectedProfile=settings_.profiles.front().name; return save(error); }
    std::ifstream in(path_); std::stringstream b; b<<in.rdbuf(); std::string s=b.str(); in.close(); if(s.empty()){error="config.json is empty.";return false;}
    settings_.llamaCommand=read(s,"llama_command",settings_.llamaCommand); settings_.host=read(s,"server_host",settings_.host); settings_.port=(unsigned short)number(s,"server_port",settings_.port); settings_.workspace=read(s,"workspace",settings_.workspace.string()); settings_.dockerImage=read(s,"docker_image",settings_.dockerImage); settings_.autoStartServer=flag(s,"auto_start_server",false); settings_.selectedProfile=read(s,"selected_profile",{});
    const auto p=s.find("\"profiles\""); const auto a=s.find('[',p); const auto z=s.find(']',a); if(a!=std::string::npos&&z!=std::string::npos){ std::string block=s.substr(a,z-a); std::regex obj("\\{([^}]*)\\}"); for(std::sregex_iterator i(block.begin(),block.end(),obj),e;i!=e;++i){const auto x=i->str(); Profile q; q.name=read(x,"name",{}); q.model=read(x,"model",{}); if(!q.name.empty()&&!q.model.empty()){q.context=number(x,"context",q.context);q.parallel=number(x,"parallel",q.parallel);q.gpuLayers=number(x,"gpu_layers",q.gpuLayers);q.cpuMoe=number(x,"cpu_moe",q.cpuMoe);q.topK=number(x,"top_k",q.topK);q.batchSize=number(x,"batch_size",q.batchSize);q.ubatchSize=number(x,"ubatch_size",q.ubatchSize);q.temperature=decimal(x,"temperature",q.temperature);q.topP=decimal(x,"top_p",q.topP);q.presencePenalty=decimal(x,"presence_penalty",q.presencePenalty);q.repeatPenalty=decimal(x,"repeat_penalty",q.repeatPenalty);q.frequencyPenalty=decimal(x,"frequency_penalty",q.frequencyPenalty);q.kvK=read(x,"kv_k",q.kvK);q.kvV=read(x,"kv_v",q.kvV);q.flashAttention=flag(x,"flash_attention",q.flashAttention);q.tools=read(x,"tools",q.tools);q.toolsRuntime=read(x,"tools_runtime",q.toolsRuntime);q.mtpSupported=flag(x,"mtp_supported",q.mtpSupported);q.specType=read(x,"spec_type",q.specType);q.specDraftNMax=number(x,"spec_draft_n_max",q.specDraftNMax);settings_.profiles.push_back(std::move(q));}} }
    if (migrateDefaultQwenContext(s, error, path_)) {
        for (auto& profile : settings_.profiles) if (profile.name == "Qwen3-Coder-30B-A3B" && (profile.context == 8192 || profile.context == 16384)) profile.context = 32768;
    } else if (!error.empty()) return false;
    if(settings_.profiles.empty()){error="config.json contains no valid profiles.";return false;} if(settings_.selectedProfile.empty())settings_.selectedProfile=settings_.profiles.front().name; return true;
}
bool ConfigManager::save(std::string& error) const { std::ofstream out(path_); if(!out){error="Cannot write "+path_.string();return false;} out<<"{\n  \"llama_command\": \""<<json(settings_.llamaCommand)<<"\",\n  \"server_host\": \""<<json(settings_.host)<<"\",\n  \"server_port\": "<<settings_.port<<",\n  \"workspace\": \""<<json(settings_.workspace.string())<<"\",\n  \"docker_image\": \""<<json(settings_.dockerImage)<<"\",\n  \"auto_start_server\": "<<(settings_.autoStartServer?"true":"false")<<",\n  \"selected_profile\": \""<<json(settings_.selectedProfile)<<"\",\n  \"profiles\": [\n"; for(size_t i=0;i<settings_.profiles.size();++i){const auto&p=settings_.profiles[i];out<<"    {\"name\": \""<<json(p.name)<<"\", \"model\": \""<<json(p.model.string())<<"\", \"context\": "<<p.context<<", \"parallel\": "<<p.parallel<<", \"gpu_layers\": "<<p.gpuLayers<<", \"cpu_moe\": "<<p.cpuMoe<<", \"temperature\": "<<p.temperature<<", \"top_k\": "<<p.topK<<", \"top_p\": "<<p.topP<<", \"presence_penalty\": "<<p.presencePenalty<<", \"repeat_penalty\": "<<p.repeatPenalty<<", \"frequency_penalty\": "<<p.frequencyPenalty<<", \"batch_size\": "<<p.batchSize<<", \"ubatch_size\": "<<p.ubatchSize<<", \"kv_k\": \""<<p.kvK<<"\", \"kv_v\": \""<<p.kvV<<"\", \"flash_attention\": "<<(p.flashAttention?"true":"false")<<", \"tools\": \""<<p.tools<<"\", \"tools_runtime\": \""<<p.toolsRuntime<<"\", \"mtp_supported\": "<<(p.mtpSupported?"true":"false")<<", \"spec_type\": \""<<json(p.specType)<<"\", \"spec_draft_n_max\": "<<p.specDraftNMax<<"}"<<(i+1<settings_.profiles.size()?",":"")<<"\n";} out<<"  ]\n}\n"; return true; }
const Profile* ConfigManager::selectedProfile() const noexcept { for(const auto&p:settings_.profiles)if(p.name==settings_.selectedProfile)return &p; return settings_.profiles.empty()?nullptr:&settings_.profiles.front(); }
}
