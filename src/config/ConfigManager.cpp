#include "config/ConfigManager.h"
#include "config/Json.h"
#include "util/Text.h"
#include <fstream>
#include <set>
#include <windows.h>

namespace lvk::config {
namespace {
Profile decode(const Json& j, bool legacy) {
    Profile p;
    p.name=j.at(legacy?"name":"display_name").str();
    p.model=util::wide(j.at(legacy?"model":"path").str());
    p.id=j.at("id").str();p.family=j.at("family").str();p.quant=j.at("quant").str();
    p.mtpModelPath=util::wide(j.at("mtp_model_path").str());
#define INT(key,member) p.member=j.at(key).integer(p.member)
#define NUM(key,member) p.member=j.at(key).num(p.member)
#define STR(key,member) p.member=j.at(key).str(p.member)
    INT("context",context);INT("max_context",maxContext);INT("parallel",parallel);INT("gpu_layers",gpuLayers);INT("cpu_moe",cpuMoe);
    INT("top_k",topK);INT("batch_size",batchSize);INT("ubatch_size",ubatchSize);INT("spec_draft_n_max",specDraftNMax);
    INT("agent_turn_limit",agentTurnLimit);
    NUM("temperature",temperature);NUM("top_p",topP);NUM("presence_penalty",presencePenalty);NUM("repeat_penalty",repeatPenalty);NUM("frequency_penalty",frequencyPenalty);
    STR("kv_k",kvK);STR("kv_v",kvV);STR("tools",tools);STR("tools_runtime",toolsRuntime);STR("spec_type",specType);
#undef INT
#undef NUM
#undef STR
    p.mtpSupported=j.at("mtp_supported").flag();
    p.flashAttention=j.at("flash_attention").str();
    if(p.flashAttention.empty())p.flashAttention=j.at("flash_attention").flag(true)?"on":"off";
    if(p.name.empty()||p.model.empty())throw std::runtime_error("A registered model has no name or path.");
    if(p.model.wstring().find(L'\0')!=std::wstring::npos)throw std::runtime_error("Invalid model path.");
    p.model=std::filesystem::absolute(p.model).lexically_normal();
    if(p.context<=0||p.maxContext<=0)throw std::runtime_error("Invalid model context.");
    if(p.agentTurnLimit!=-1&&p.agentTurnLimit!=0&&p.agentTurnLimit!=10&&p.agentTurnLimit!=20&&p.agentTurnLimit!=50&&p.agentTurnLimit!=100)
        throw std::runtime_error("Invalid agent turn limit.");
    return p;
}
Json encode(const Profile& p) {
    return Json::Object{
        {"id",p.id},{"display_name",p.name},{"path",util::pathText(p.model)},{"family",p.family},{"quant",p.quant},
        {"mtp_model_path",util::pathText(p.mtpModelPath)},{"context",p.context},{"max_context",p.maxContext},
        {"parallel",p.parallel},{"gpu_layers",p.gpuLayers},{"cpu_moe",p.cpuMoe},
        {"temperature",p.temperature},{"top_k",p.topK},{"top_p",p.topP},{"presence_penalty",p.presencePenalty},
        {"repeat_penalty",p.repeatPenalty},{"frequency_penalty",p.frequencyPenalty},{"batch_size",p.batchSize},
        {"ubatch_size",p.ubatchSize},{"kv_k",p.kvK},{"kv_v",p.kvV},{"flash_attention",p.flashAttention},
        {"tools",p.tools},{"tools_runtime",p.toolsRuntime},{"agent_turn_limit",p.agentTurnLimit},{"mtp_supported",p.mtpSupported},
        {"spec_type",p.specType},{"spec_draft_n_max",p.specDraftNMax}
    };
}
std::filesystem::path defaultWorkspace(const std::filesystem::path& configPath) {
    auto candidate=configPath.parent_path()/L"workspace";
    std::error_code ec;
    std::filesystem::create_directories(candidate,ec);
    if(!ec)return candidate;
    wchar_t localAppData[32768]{};
    constexpr DWORD capacity=static_cast<DWORD>(_countof(localAppData));
    const DWORD length=GetEnvironmentVariableW(L"LOCALAPPDATA",localAppData,capacity);
    if(length>0&&length<capacity){
        candidate=std::filesystem::path(localAppData)/L"AI-Agent-LVK"/L"workspace";
        ec.clear();std::filesystem::create_directories(candidate,ec);
        if(!ec)return candidate;
    }
    return configPath.parent_path()/L"workspace";
}
}
ConfigManager::ConfigManager(std::filesystem::path directory):path_(std::move(directory)/L"config.json") {}
bool ConfigManager::load(std::string& error) {
    error.clear();
    try {
        Settings next;
        next.workspace=defaultWorkspace(path_);
        if(!std::filesystem::exists(path_)){
            // Discover the pre-existing installation only on first run. Never invent a downloaded model.
            Profile old;old.name="Qwen3-Coder-30B-A3B";old.id="legacy-1";
            old.model=LR"(G:\AI\models\Qwen3-Coder-30B-A3B\Qwen3-Coder-30B-A3B-Instruct-Q4_K_M.gguf)";
            if(std::filesystem::is_regular_file(old.model)){next.profiles.push_back(old);next.selectedProfile=old.id;}
            std::error_code workspaceError;std::filesystem::create_directories(next.workspace,workspaceError);
            if(workspaceError)throw std::runtime_error("Cannot create workspace: "+util::pathText(next.workspace)+" ("+workspaceError.message()+")");
            settings_=std::move(next);return save(error);
        }
        std::ifstream in(path_,std::ios::binary);
        if(!in)throw std::runtime_error("Cannot read config.json.");
        if(std::filesystem::file_size(path_)>16*1024*1024)throw std::runtime_error("config.json is too large.");
        std::string text((std::istreambuf_iterator<char>(in)),{});
        in.close(); // Windows cannot replace a config still open by this reader.
        const Json root=Json::parse(text);
        if(!std::holds_alternative<Json::Object>(root.value))throw std::runtime_error("Configuration must be a JSON object.");
        next.llamaCommand=root.at("llama_command").str(next.llamaCommand);
        next.host=root.at("server_host").str(next.host);
        const int port=root.at("server_port").integer(next.port);
        if(port<1||port>65535)throw std::runtime_error("Invalid server port.");
        next.port=static_cast<unsigned short>(port);
        auto workspaceText=root.at("workspace_path").str(root.at("workspace").str(util::pathText(next.workspace)));
        if(workspaceText.empty())next.workspace=defaultWorkspace(path_);else next.workspace=util::wide(workspaceText);
        next.dockerImage=root.at("docker_image").str(next.dockerImage);
        next.autoStartServer=root.at("auto_start_server").flag();
        next.lastModelDownloadDirectory=util::wide(root.at("last_model_download_directory").str());
        const bool legacy=!root.has("installed_models");
        next.selectedProfile=root.at(legacy?"selected_profile":"active_model_id").str();
        const auto& models=root.at(legacy?"profiles":"installed_models");
        if(!legacy&&!std::holds_alternative<Json::Array>(models.value))throw std::runtime_error("installed_models must be an array.");
        std::set<std::string> ids;
        bool capabilityUpdated=false;
        for(const auto& item:models.array()){
            auto p=decode(item,legacy);
            if(p.family=="qwen35moe"&&p.quant=="Q4_K_M"&&p.maxContext<262144){p.maxContext=262144;capabilityUpdated=true;}
            if(p.id.empty())p.id="legacy-"+std::to_string(next.profiles.size()+1);
            if(!ids.insert(p.id).second)throw std::runtime_error("Duplicate model id in config.");
            if(legacy&&p.name==next.selectedProfile)next.selectedProfile=p.id;
            next.profiles.push_back(std::move(p));
        }
        if(legacy&&next.profiles.empty()&&root.has("model_path")){
            Json::Object old=std::get<Json::Object>(root.value);
            old["name"]=root.at("model_name").str("Existing model");
            old["model"]=root.at("model_path");
            auto p=decode(Json(old),true);p.id="legacy-1";next.selectedProfile=p.id;next.profiles.push_back(p);
        }
        if(legacy&&next.profiles.empty())throw std::runtime_error("Legacy config has no valid model profile.");
        if(!next.profiles.empty()&&!ids.contains(next.selectedProfile))next.selectedProfile=next.profiles.front().id;
        settings_=std::move(next);
        std::error_code workspaceError;
        std::filesystem::create_directories(settings_.workspace,workspaceError);
        if(workspaceError)throw std::runtime_error("Cannot create workspace: "+util::pathText(settings_.workspace)+" ("+workspaceError.message()+")");
        if(legacy){
            // Keep the exact old bytes for recovery; do not replace a previous migration backup.
            auto backup=path_;backup+=L".pre-models.bak";
            if(!std::filesystem::exists(backup))std::filesystem::copy_file(path_,backup);
            return save(error);
        }
        if(capabilityUpdated)return save(error);
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ConfigManager::save(std::string& error) const {
    error.clear();
    try {
        Json::Array models;for(const auto& p:settings_.profiles)models.push_back(encode(p));
        const auto workspace=settings_.workspace.empty()?path_.parent_path()/L"workspace":settings_.workspace;
        Json root=Json::Object{{"schema_version",2},{"llama_command",settings_.llamaCommand},{"server_host",settings_.host},
            {"server_port",static_cast<int>(settings_.port)},{"workspace_path",util::pathText(workspace)},{"docker_image",settings_.dockerImage},
            {"auto_start_server",settings_.autoStartServer},{"active_model_id",settings_.selectedProfile},
            {"last_model_download_directory",util::pathText(settings_.lastModelDownloadDirectory)},{"installed_models",models}};
        const auto data=root.dump()+"\n";
        auto temp=path_;temp+=L".tmp";
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<data;out.flush();if(!out)throw std::runtime_error("Cannot write config.json temporary file.");}
        if(!MoveFileExW(temp.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace config.json: "+util::winError(GetLastError()));
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
const Profile* ConfigManager::selectedProfile() const noexcept {
    for(const auto& p:settings_.profiles)if(p.id==settings_.selectedProfile)return &p;
    return nullptr;
}
}
