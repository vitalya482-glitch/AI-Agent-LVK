#include <winsock2.h>
#include <ws2tcpip.h>
#include "config/ConfigManager.h"
#include "config/Json.h"
#include "models/ModelCatalog.h"
#include "models/ModelDownloader.h"
#include "launcher/LlamaManager.h"
#include "docker/DockerManager.h"
#include "util/Text.h"
#include <shellapi.h>
#include <fstream>
#include <iostream>
#include <thread>

using namespace lvk;
namespace fs=std::filesystem;
namespace {
int checks=0;
void require(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
void write(const fs::path& path,const std::string& value){std::ofstream out(path,std::ios::binary);out<<value;if(!out)throw std::runtime_error("Test write failed.");}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);return {(std::istreambuf_iterator<char>(in)),{}};}
// Tiny one-request localhost fixture: tests exercise the real WinHTTP transport,
// streaming writes, hash, cancellation and commit. No large/public downloads.
class Server {
    SOCKET listener=INVALID_SOCKET;
    std::thread thread;
public:
    unsigned short port{};
    Server(std::string body,int status=200,bool length=true,bool slow=false,std::string extraHeaders={}){
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);require(listener!=INVALID_SOCKET,"fixture socket");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        require(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"fixture bind");
        int count=sizeof(address);getsockname(listener,reinterpret_cast<sockaddr*>(&address),&count);port=ntohs(address.sin_port);
        listen(listener,1);
        thread=std::thread([this,body=std::move(body),status,length,slow,extraHeaders=std::move(extraHeaders)]{
            fd_set ready;FD_ZERO(&ready);FD_SET(listener,&ready);timeval timeout{10,0};
            if(select(0,&ready,nullptr,nullptr,&timeout)<=0)return;
            SOCKET client=accept(listener,nullptr,nullptr);if(client==INVALID_SOCKET)return;
            DWORD ioTimeout=5000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&ioTimeout),sizeof(ioTimeout));
            char request[8192]{};recv(client,request,sizeof(request),0);
            const auto header="HTTP/1.1 "+std::to_string(status)+" Test\r\nConnection: close\r\n"+(length?"Content-Length: "+std::to_string(body.size())+"\r\n":"")+extraHeaders+"\r\n";
            send(client,header.data(),static_cast<int>(header.size()),0);
            for(size_t pos=0;pos<body.size();){
                const auto amount=static_cast<int>(std::min<size_t>(16384,body.size()-pos));
                const int sent=send(client,body.data()+pos,amount,0);if(sent<=0)break;pos+=static_cast<size_t>(sent);
                if(slow)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            shutdown(client,SD_SEND);closesocket(client);
        });
    }
    ~Server(){if(thread.joinable())thread.join();closesocket(listener);}
    std::string url()const{return "http://127.0.0.1:"+std::to_string(port)+"/model";}
};
void migration(const fs::path& root){
    fs::create_directories(root);
    const std::string legacy=R"({"selected_profile":"Second","workspace":"G:\\work space\\\u043c\u043e\u0434\u0435\u043b\u0438","profiles":[
      {"name":"First","model":"G:\\models\\first.gguf","context":8192},
      {"name":"Second","model":"G:\\models\\\u6a21\u578b two.gguf","context":16384,"max_context":65536,"temperature":0.82,"top_k":45,"top_p":0.88,"presence_penalty":0.13,"repeat_penalty":1.07,"frequency_penalty":0.21,"batch_size":384,"ubatch_size":192,"parallel":2,"gpu_layers":61,"cpu_moe":19,"kv_k":"f16","kv_v":"q4_0","flash_attention":false,"mtp_supported":true,"spec_type":"draft-mtp","spec_draft_n_max":3,"tools":"all","tools_runtime":"docker:custom"}]})";
    write(root/L"config.json",legacy);config::ConfigManager manager(root);std::string error;
    require(manager.load(error),error);require(manager.settings().profiles.size()==2,"migration count");
    const auto* p=manager.selectedProfile();require(p&&p->name=="Second","active migration");
    require(p->context==16384&&manager.settings().profiles.front().context==8192,"context must not be reset");
    require(p->temperature==.82&&p->topK==45&&p->topP==.88,"sampling preserved");
    require(p->presencePenalty==.13&&p->repeatPenalty==1.07&&p->frequencyPenalty==.21,"penalties preserved");
    require(p->batchSize==384&&p->ubatchSize==192&&p->parallel==2&&p->gpuLayers==61&&p->cpuMoe==19,"hardware tuning preserved");
    require(p->kvK=="f16"&&p->kvV=="q4_0"&&p->flashAttention=="off"&&p->mtpSupported&&p->specType=="draft-mtp"&&p->specDraftNMax==3,"KV / FA / MTP preserved");
    require(p->maxContext==65536&&p->toolsRuntime=="docker:custom","capability / tools preserved");
    require(p->model==fs::path(L"G:\\models\\模型 two.gguf"),"Unicode path migration");
    require(read(root/L"config.json.pre-models.bak")==legacy,"original backup exact bytes");
    const auto firstSave=read(root/L"config.json");config::ConfigManager reloaded(root);require(reloaded.load(error),error);
    require(reloaded.selectedProfile()->id==p->id,"stable active ID");require(reloaded.save(error),error);require(read(root/L"config.json")==firstSave,"migration roundtrip idempotence");
    write(root/L"config.json","{broken");require(!reloaded.load(error),"invalid JSON rejected");require(read(root/L"config.json")=="{broken","invalid config never overwritten");
    write(root/L"config.json",R"({"model_path":"G:\\legacy.gguf","context":4096,"temperature":0.72})");
    require(manager.load(error),error);require(manager.selectedProfile()->context==4096&&manager.selectedProfile()->temperature==.72,"single model_path migration");
}
void freshWorkspace(const fs::path& root){
    fs::create_directories(root);config::ConfigManager manager(root);std::string error;
    require(manager.load(error),error);require(manager.settings().workspace==root/L"workspace","fresh workspace is beside launcher");
    require(fs::is_directory(root/L"workspace"),"fresh workspace created");
    config::ConfigManager reloaded(root);require(reloaded.load(error),error);require(reloaded.settings().workspace==root/L"workspace","workspace path persists");
}
void registry(const fs::path& root){
    fs::create_directories(root);const auto path=root/L"模型 with spaces.gguf";write(path,"GGUFtest");
    config::Settings settings;models::registerModel(settings,models::fromExisting(path));
    require(settings.profiles.size()==1&&settings.profiles.front().model==path,"existing registration no copy");
    settings.profiles.front().temperature=.71;const auto id=settings.selectedProfile;
    models::registerModel(settings,models::fromExisting(path));require(settings.profiles.size()==1&&settings.profiles.front().temperature==.71&&settings.selectedProfile==id,"dedup preserves tuning");
    const auto& entry=models::catalog().front();auto qwen=models::fromCatalog(entry,root/L"Qwen model.gguf");
    require(qwen.context==8192&&qwen.maxContext==262144&&!qwen.mtpSupported&&qwen.mtpModelPath.empty()&&qwen.agentTurnLimit==0,"catalog context capability");
    const auto offQwen=qwen;
    qwen.agentTurnLimit=20;
    models::registerModel(settings,qwen);require(settings.selectedProfile==qwen.id,"new model selection");
    config::ConfigManager manager(root);manager.settings()=settings;std::string error;require(manager.save(error),error);
    require(manager.load(error),error);require(manager.selectedProfile()->model==qwen.model&&manager.selectedProfile()->agentTurnLimit==20,"selection and agent limit survive restart");
    process::ProcessManager process;log::LogManager logs(root/L"logs");launcher::LlamaManager llama(process,logs);
    auto line=llama.commandLine(settings,qwen);int argc{};auto argv=CommandLineToArgvW((L"llama "+line).c_str(),&argc);
    require(argv&&argc>3&&std::wstring(argv[3])==qwen.model.wstring(),"selected model path quoted");LocalFree(argv);
    require(line.find(L"-c 8192 -np 1 -ngl 999 -ncmoe 27")!=std::wstring::npos,"Qwen runtime defaults");
    require(llama.commandLine(settings,offQwen).find(L"--ui-config")==std::wstring::npos,"Off does not override Web UI agent turns");
    require(line.find(L"--ui-config")!=std::wstring::npos&&line.find(L"agenticMaxTurns")!=std::wstring::npos,"agent turn limit uses Web UI config");
    qwen.agentTurnLimit=-1;require(llama.commandLine(settings,qwen).find(L"Infinity")!=std::wstring::npos,"Unlimited uses Web UI Infinity config");
    require(line.find(L"--host \"127.0.0.1\"")!=std::wstring::npos&&line.find(L"--spec-type")==std::wstring::npos,"localhost / MTP disabled");
    qwen.mtpSupported=true;qwen.specType="draft-mtp";require(llama.commandLine(settings,qwen).find(L"--spec-type")==std::wstring::npos,"MTP missing draft stays disabled");
    settings.llamaCommand="C:\\llama runtime\\llama-server.exe";require(llama.commandLine(settings,qwen).starts_with(L"-m "),"direct llama-server omits serve subcommand");
    const auto quoted=util::quote(L"G:\\目录 space\\");argv=CommandLineToArgvW((L"exe "+quoted).c_str(),&argc);require(argc==2&&std::wstring(argv[1])==L"G:\\目录 space\\","Windows trailing slash quote");LocalFree(argv);
    settings.profiles.clear();settings.selectedProfile.clear();require(fs::exists(path),"remove registration leaves file");
    bool rejected=false;try{models::fromExisting(root/L"not.gguf.part");}catch(...){rejected=true;}require(rejected,"part cannot be added");
    docker::DockerManager docker("ai-cpp-sandbox");const auto args=util::utf8(docker.sandboxArguments(root));
    require(args.find("-v ")!=std::string::npos&&args.find(":/workspace")!=std::string::npos,"workspace bind mount");
    require(args.find("-w /workspace")!=std::string::npos&&args.find("--read-only")!=std::string::npos,"workspace workdir and read-only root");
    require(args.find("--tmpfs /tmp")!=std::string::npos&&args.find("--cap-drop ALL")!=std::string::npos&&args.find("no-new-privileges")!=std::string::npos,"sandbox restrictions");
}
void ownedProcess(const fs::path& root){
    fs::create_directories(root);const auto path=root/L"选定 модель.gguf";write(path,"GGUFtest");
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    config::Settings settings;settings.llamaCommand=util::utf8(executable);auto model=models::fromExisting(path);
    process::ProcessManager child,unrelated;log::LogManager logs(root/L"logs");launcher::LlamaManager llama(child,logs);std::string error;
    require(unrelated.start(executable,L"--sentinel",{},[](const auto&){}),"sentinel starts");
    require(llama.start(settings,model,error),error);
    require(WaitForSingleObject(child.processHandle(),5000)==WAIT_OBJECT_0,"fake runtime completes");
    auto capture=path;capture+=L".argv";
    const auto args=read(capture);require(args.find(util::pathText(path))!=std::string::npos,"selected Unicode model reaches child argv");
    require(args.find("NO_VISIBLE_CONSOLE")!=std::string::npos,"child process has no visible console");
    require(llama.start(settings,model,error),"start after previous natural exit");
    llama.stop();require(unrelated.running(),"Stop only terminates owned llama process");unrelated.stop();
}
void downloads(const fs::path& root){
    fs::create_directories(root);std::atomic<bool> cancel=false;
    auto request=models::DownloadRequest{models::catalog().front(),root};request.entry.filename="small.gguf";request.entry.expectedSize=8;
    request.entry.sha256="30be5a60b7786b878803d49ff1dcaa157e9c098b896ff8bca692104fa9df3ba0";
    auto noop=[](const models::DownloadProgress&){};
    {Server server("GGUFtest");request.entry.downloadUrl=server.url();auto r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::Complete,r.message);require(read(r.path)=="GGUFtest"&&!fs::exists(root/L"small.gguf.part"),"hash verified atomic commit");}
    auto r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::NeedExistingChoice,"existing requires explicit choice");
    request.existing=models::ExistingPolicy::UseExisting;r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::Complete,"use existing verifies hash");
    request.existing=models::ExistingPolicy::Replace;
    {Server server("GGUFbad!");request.entry.downloadUrl=server.url();r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::Failed&&r.message.find("hash mismatch")!=std::string::npos,"bad hash rejected");require(read(root/L"small.gguf")=="GGUFtest","failure preserves old final");}
    r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::NeedPartialChoice,"partial requires restart approval");
    request.restartPartial=true;
    {Server server("GGUFtest");request.entry.downloadUrl=server.url();r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::Complete,"explicit restart partial");}
    request.entry.filename="unknown-size.gguf";request.existing=models::ExistingPolicy::Ask;request.restartPartial=false;
    bool sawIndeterminate=false;
    {Server server("GGUFtest",200,false);request.entry.downloadUrl=server.url();r=models::downloadModel(request,cancel,[&](const auto& progress){if(progress.bytes==8&&progress.total==0)sawIndeterminate=true;});require(r.status==models::DownloadStatus::Complete&&sawIndeterminate,"no content-length indeterminate with real bytes");}
    request.entry.filename="http-error.gguf";
    {Server server("denied",403);request.entry.downloadUrl=server.url();r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::Failed&&r.message.find("403")!=std::string::npos,"HTTP error text");require(!fs::exists(root/L"http-error.gguf"),"HTTP error never installed");}
    request.entry.filename="redirect.gguf";
    {Server destination("GGUFtest");Server redirect("",302,true,false,"Location: "+destination.url()+"\r\n");request.entry.downloadUrl=redirect.url();r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::Complete,"HTTP redirect follows safely");}
    request.entry.filename="truncated.gguf";request.entry.expectedSize=0;request.entry.sha256.clear();
    {Server server("GGUF",200,false,false,"Content-Length: 100\r\n");request.entry.downloadUrl=server.url();r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::Failed,"truncated response never committed");}
    request.entry.filename="cancel.gguf";request.entry.expectedSize=4*1024*1024;request.entry.sha256.clear();
    {std::string body(static_cast<size_t>(request.entry.expectedSize),'x');body.replace(0,4,"GGUF");Server server(body,200,true,true);request.entry.downloadUrl=server.url();
        r=models::downloadModel(request,cancel,[&](const auto& progress){if(progress.bytes>0)cancel=true;});require(r.status==models::DownloadStatus::Cancelled,"cancel real network transfer");require(!fs::exists(root/L"cancel.gguf")&&fs::exists(root/L"cancel.gguf.part"),"cancel retains partial only");}
    cancel=false;request.entry.filename="../outside.gguf";r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::Failed,"traversal rejected");
    request.entry.filename="bad.exe";r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::Failed,"executable catalog rejected");
    request.entry.filename="space.gguf";request.entry.expectedSize=UINT64_MAX;r=models::downloadModel(request,cancel,noop);require(r.status==models::DownloadStatus::Failed&&r.message.find("disk space")!=std::string::npos,"disk preflight overflow safe");
}
}
int wmain(int argc,wchar_t** argv){
    if(argc>1&&std::wstring(argv[1])==L"--sentinel"){Sleep(15000);return 0;}
    if(argc>3&&std::wstring(argv[1])==L"serve"){
        fs::path path(argv[3]);path+=L".argv";std::string args;
        for(int i=1;i<argc;++i)args+=util::utf8(argv[i])+"\n";
        args+=IsWindowVisible(GetConsoleWindow())?"VISIBLE_CONSOLE":"NO_VISIBLE_CONSOLE";
        write(path,args);Sleep(300);return 0;
    }
    try{
        WSADATA data{};require(WSAStartup(MAKEWORD(2,2),&data)==0,"Winsock init");
        const auto root=fs::current_path()/util::wide("model-test-"+models::newModelId());fs::create_directory(root);
        freshWorkspace(root/L"fresh");migration(root/L"migration");registry(root/L"registry");ownedProcess(root/L"process");downloads(root/L"downloads");
        std::cout<<"PASS: "<<checks<<" checks. Fixtures: "<<util::pathText(root)<<"\n";WSACleanup();return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}
}
