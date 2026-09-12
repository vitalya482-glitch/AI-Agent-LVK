#include "dependencies/DependencyChecker.h"
#include "port/PortChecker.h"
#include "process/ProcessRunner.h"
#include "util/Text.h"

namespace lvk::dependencies {
namespace {
bool succeeds(const std::filesystem::path& executable,const std::wstring& arguments){const auto result=process::runHidden(executable,arguments);return result.started&&result.exitCode==0;}
}
std::vector<Item> checkStatic(const config::Settings& s,const config::Profile* p,bool includeDocker){
    std::vector<Item> result;
    const bool llama=!process::findOnPath(util::wide(s.llamaCommand)).empty();
    result.push_back({"llama",llama?"Found":"llama executable not found: "+s.llamaCommand,llama});
    const auto docker=includeDocker?process::findOnPath(L"docker"):std::filesystem::path{};
    if(includeDocker)result.push_back({"Docker CLI",docker.empty()?"Docker CLI not found":"Found",!docker.empty()});
    std::error_code ec;const bool model=p&&std::filesystem::is_regular_file(p->model,ec);
    result.push_back({"Model",model?"Found":p?"Model file not found: "+util::pathText(p->model):"No installed model selected. Use Add Existing Model or Download Model.",model});
    const bool workspace=std::filesystem::is_directory(s.workspace,ec);
    result.push_back({"Workspace",workspace?"Found":"Workspace does not exist",workspace});
    if(includeDocker){const bool image=!docker.empty()&&succeeds(docker,L"image inspect "+util::quote(util::wide(s.dockerImage)));
        result.push_back({"Docker image",image?"Ready":"Docker image is missing: "+s.dockerImage,image});}
    return result;
}
std::vector<Item> checkRuntime(const config::Settings& s,bool llamaProcessRunning,bool includeDocker){
    std::vector<Item> result;
    if(includeDocker){const auto docker=process::findOnPath(L"docker");
    const bool engine=!docker.empty()&&succeeds(docker,L"info");
    result.push_back({"Docker Engine",engine?"Running":"Docker Engine is installed but not running",engine});}
    const bool listening=port::isListening(s.host,s.port),ownServer=listening&&llamaProcessRunning;
    result.push_back({"Port",ownServer?std::to_string(s.port)+" in use by server":listening?"Busy (another process)":"Free",ownServer||!listening});
    result.push_back({"Server",ownServer?"Running":llamaProcessRunning?"Starting":"Stopped",ownServer});return result;
}
}
