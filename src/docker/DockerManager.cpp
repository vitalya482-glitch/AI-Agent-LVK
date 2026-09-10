#include "docker/DockerManager.h"
#include "process/ProcessRunner.h"
#include "util/Text.h"
#include <windows.h>
#include <filesystem>
#include <fstream>

namespace lvk::docker {
namespace {
std::wstring q(const std::string& value){return util::quote(util::wide(value));}
std::filesystem::path desktopPath(){
    wchar_t programFiles[32768]{};const DWORD length=GetEnvironmentVariableW(L"ProgramFiles",programFiles,static_cast<DWORD>(std::size(programFiles)));
    if(length>0&&length<std::size(programFiles))return std::filesystem::path(std::wstring(programFiles,length))/L"Docker/Docker/Docker Desktop.exe";
    return L"C:\\Program Files\\Docker\\Docker\\Docker Desktop.exe";
}
std::string trim(std::string value){while(!value.empty()&&(value.back()=='\r'||value.back()=='\n'||value.back()==' '||value.back()=='\t'))value.pop_back();while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.erase(value.begin());return value;}
bool ensurePolicy(const std::filesystem::path& workspace,std::string& error){
    const auto policy=workspace/L"AGENTS.md";if(std::filesystem::exists(policy))return true;
    std::ofstream out(policy,std::ios::binary|std::ios::trunc);
    if(!out){error="Cannot create workspace policy file: "+util::pathText(policy);return false;}
    out<<"# AI-Agent-LVK coding workspace policy\n\n"
          "Persistent project files, source files, build outputs and deliverables MUST be created under /workspace.\n"
          "Use /tmp only for temporary intermediate files. At the beginning of a task inspect /workspace, then create or select a project directory under /workspace.\n"
          "Do not place persistent project files in /tmp, /root, /home, /opt, /usr, or another container path.\n"
          "At the end report the project path inside /workspace. AGENT_WORKSPACE is /workspace.\n";
    return static_cast<bool>(out);
}
}
DockerManager::~DockerManager(){std::string ignored;removeSandbox(ignored);}
bool DockerManager::engineRunning()const{const auto docker=process::findOnPath(L"docker");return !docker.empty()&&process::runHidden(docker,L"info").exitCode==0;}
bool DockerManager::startDesktop(std::string& error)const{
    if(engineRunning())return true;const auto desktop=desktopPath();
    if(!std::filesystem::is_regular_file(desktop)){error="Docker Desktop executable was not found.";return false;}
    return process::startHidden(desktop,L"",desktop.parent_path(),error);
}
bool DockerManager::imageReady()const{const auto docker=process::findOnPath(L"docker");return !docker.empty()&&process::runHidden(docker,L"image inspect "+q(image_)).exitCode==0;}
std::wstring DockerManager::sandboxArguments(const std::filesystem::path& workspace)const{
    const auto name=L"ai-agent-lvk-sandbox-"+std::to_wstring(GetCurrentProcessId());
    return L"create --name "+name+L" --label ai-agent-lvk.owner=launcher --read-only --tmpfs /tmp --tmpfs /root/.cache --cap-drop ALL --security-opt no-new-privileges -v "+util::quote(workspace.wstring()+L":/workspace")+L" -w /workspace "+q(image_)+L" sleep infinity";
}
bool DockerManager::ensureSandbox(const std::filesystem::path& workspace,std::string& error){
    if(!std::filesystem::is_directory(workspace)){error="Workspace directory does not exist: "+util::pathText(workspace);return false;}
    if(!ensurePolicy(workspace,error))return false;
    if(!engineRunning()){error="Docker Engine is not running.";return false;}
    if(!imageReady()){error="Docker image is missing: "+image_;return false;}
    if(!containerId_.empty())return true;
    const auto docker=process::findOnPath(L"docker");if(docker.empty()){error="Docker CLI not found.";return false;}
    const auto created=process::runHidden(docker,sandboxArguments(workspace));
    if(!created.started||created.exitCode!=0){error="Docker cannot access the selected workspace: "+util::pathText(workspace)+". Check Docker Desktop file sharing / drive access.";if(!created.output.empty())error+="\n"+created.output;return false;}
    containerId_=trim(created.output);if(containerId_.empty()){error="Docker did not return a sandbox container ID.";return false;}
    const auto started=process::runHidden(docker,L"start "+q(containerId_));
    if(!started.started||started.exitCode!=0){error="Docker sandbox could not start.";if(!started.output.empty())error+="\n"+started.output;std::string ignored;removeSandbox(ignored);return false;}
    return true;
}
bool DockerManager::removeSandbox(std::string& error){
    if(containerId_.empty())return true;const auto docker=process::findOnPath(L"docker");if(docker.empty()){error="Docker CLI not found while removing owned sandbox.";return false;}
    const auto result=process::runHidden(docker,L"rm -f "+q(containerId_));if(!result.started||result.exitCode!=0){error="Owned Docker sandbox could not be removed.";if(!result.output.empty())error+="\n"+result.output;return false;}containerId_.clear();return true;
}
std::string DockerManager::runtimeSpec()const{return containerId_.empty()?"":("docker-container:"+containerId_);}
bool DockerManager::rebuild(const std::filesystem::path& folder,std::string& error)const{const auto docker=process::findOnPath(L"docker");if(docker.empty()){error="Docker CLI not found.";return false;}const auto result=process::runHidden(docker,L"build -t "+q(image_)+L" "+util::quote(folder.wstring()));if(!result.started||result.exitCode!=0){error="docker build failed.";if(!result.output.empty())error+="\n"+result.output;return false;}return true;}
}
