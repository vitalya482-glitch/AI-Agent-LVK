#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "config/ConfigManager.h"
#include "dependencies/DependencyChecker.h"
#include "docker/DockerManager.h"
#include "launcher/LlamaManager.h"
#include "log/LogManager.h"
#include "memory/MemoryMonitor.h"
#include "metrics/LlamaMetrics.h"
#include "port/PortChecker.h"
#include "process/BackgroundWorker.h"
#include "ui/ModelSettingsWindow.h"
#include "ui/DockerSettingsWindow.h"
#include "ui/DownloadWindow.h"
#include "ui/FilePicker.h"
#include "models/ModelCatalog.h"
#include "util/Text.h"
#include "update/UpdateCloseBridge.h"
#include "update/UpdateManager.h"

#ifndef AI_AGENT_LVK_VERSION
#define AI_AGENT_LVK_VERSION "0.0.0-dev"
#endif
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
using namespace lvk;
constexpr int kProfile=100,kStart=101,kStop=102,kRestart=103,kWeb=104,kBrowseModels=105,kModels=106,kUpdate=108,kLog=109,kModelSettings=110,kDockerSettings=111;
constexpr int kDownload=112,kAddExisting=113,kLocate=114,kRemove=115;
constexpr UINT kModelResult=WM_APP+5,kShutdownComplete=WM_APP+6,kCloseTimer=5;
constexpr UINT kStaticTimer=1,kRuntimeTimer=2,kLogTimer=3,kMemoryTimer=4,kStaticResult=WM_APP+1,kRuntimeResult=WM_APP+2,kOperationResult=WM_APP+3,kMemoryResult=WM_APP+4,kDockerStatusResult=WM_APP+7;
HWND gProfile{},gBrowseModels{},gStatus{},gMemory{},gLog{};
std::vector<HWND> gButtons;
std::unique_ptr<config::ConfigManager> gConfig;
std::unique_ptr<log::LogManager> gLogs;
std::unique_ptr<process::ProcessManager> gProcess;
std::unique_ptr<launcher::LlamaManager> gLlama;
std::unique_ptr<update::UpdateCloseBridge> gBridge;
std::unique_ptr<process::BackgroundWorker> gWorker;
std::unique_ptr<docker::DockerManager> gSandbox;
metrics::LlamaMetrics gLlamaMetrics;
std::unique_ptr<ui::DownloadWindow> gDownload;
std::unique_ptr<ui::DockerSettingsWindow> gDockerSettings;
std::atomic<bool> gClosing{false};
bool gShutdownQueued=false,gPickerOpen=false;
std::atomic<bool> gStaticCheckRunning{false},gRuntimeCheckRunning{false},gMemoryCheckRunning{false},gDockerCheckRunning{false},gOperationRunning{false};
std::mutex gLogMutex;
std::deque<std::string> gPendingLog;
std::vector<dependencies::Item> gStaticItems,gRuntimeItems;
memory::Snapshot gMemorySnapshot;
bool gProcessRunning=false,gVramMessageLogged=false;

std::filesystem::path appDir(){wchar_t buffer[32768]{};const DWORD n=GetModuleFileNameW(nullptr,buffer,32768);return n?std::filesystem::path(std::wstring(buffer,n)).parent_path():std::filesystem::current_path();}
void openPath(const std::wstring& path);
void rebuildSandbox(HWND window);
void changeWorkspace(HWND window);
std::wstring wide(const std::string& text){try{return util::wide(text);}catch(...){return L"[Invalid UTF-8 output]";}}
void queueLog(const std::string& text){std::lock_guard lock(gLogMutex);gPendingLog.push_back(text);}
void flushLog(){std::deque<std::string> pending;{std::lock_guard lock(gLogMutex);pending.swap(gPendingLog);}if(pending.empty()||!gLog)return;std::string text;for(const auto& line:pending){text+=line;text+="\r\n";}const auto wideText=wide(text);const LRESULT length=SendMessageW(gLog,WM_GETTEXTLENGTH,0,0);SendMessageW(gLog,EM_SETSEL,length,length);SendMessageW(gLog,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(wideText.c_str()));const LRESULT lines=SendMessageW(gLog,EM_GETLINECOUNT,0,0);if(lines>8000){const LRESULT position=SendMessageW(gLog,EM_LINEINDEX,lines-6000,0);SendMessageW(gLog,EM_SETSEL,0,position);SendMessageW(gLog,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(L""));}}
void renderStatus(){std::ostringstream output;for(const auto& item:gStaticItems)output<<item.name<<": "<<(item.ok?"OK":"ERROR")<<" - "<<item.message<<"\r\n";for(const auto& item:gRuntimeItems)output<<item.name<<": "<<(item.ok?"OK":"ERROR")<<" - "<<item.message<<"\r\n";SetWindowTextW(gStatus,wide(output.str()).c_str());}
struct Snapshot{config::Settings settings;std::optional<config::Profile> profile;};
Snapshot snapshot(){Snapshot result;result.settings=gConfig->settings();if(const auto* p=gConfig->selectedProfile())result.profile=*p;return result;}
struct StaticResult{std::vector<dependencies::Item> items;std::vector<config::Profile> models;};
void refreshModels(){
    SendMessageW(gProfile,CB_RESETCONTENT,0,0);int index=0,selected=-1;
    for(const auto& p:gConfig->settings().profiles){const auto name=wide(p.name+(p.missing?" [Missing]":""));SendMessageW(gProfile,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(p.id==gConfig->settings().selectedProfile)selected=index;++index;}
    SendMessageW(gProfile,CB_SETCURSEL,static_cast<WPARAM>(selected),0);
}
void persistConfig(){const auto settings=gConfig->settings();gWorker->submit([settings]{config::ConfigManager manager(appDir());manager.settings()=settings;std::string error;if(!manager.save(error)&&gLogs)gLogs->write("Configuration was not saved: "+error);});}
void requestStatic(HWND window){
    if(gClosing)return;bool expected=false;if(!gStaticCheckRunning.compare_exchange_strong(expected,true))return;auto state=snapshot();
    gWorker->submit([window,state=std::move(state)]()mutable{
        auto result=std::make_unique<StaticResult>();
        try{result->items=dependencies::checkStatic(state.settings,state.profile?&*state.profile:nullptr,false);
            result->models=state.settings.profiles;for(auto& p:result->models){std::error_code ec;p.missing=!std::filesystem::is_regular_file(p.model,ec);p.mtpFileAvailable=!p.mtpModelPath.empty()&&std::filesystem::is_regular_file(p.mtpModelPath,ec);}
        }catch(const std::exception& e){result->items.push_back({"Models",e.what(),false});}
        gStaticCheckRunning=false;PostMessageW(window,kStaticResult,0,reinterpret_cast<LPARAM>(result.release()));
    });
}
void requestRuntime(HWND window){bool expected=false;if(!gRuntimeCheckRunning.compare_exchange_strong(expected,true))return;const auto settings=gConfig->settings();gWorker->submit([window,settings](){const bool running=gLlama->running();auto items=dependencies::checkRuntime(settings,running,false);gRuntimeCheckRunning=false;auto* result=new std::pair<std::vector<dependencies::Item>,bool>(std::move(items),running);PostMessageW(window,kRuntimeResult,0,reinterpret_cast<LPARAM>(result));});}
std::string dockerStatusText(const std::vector<dependencies::Item>& staticItems,const std::vector<dependencies::Item>& runtimeItems){std::ostringstream output;for(const auto& item:staticItems)output<<item.name<<": "<<(item.ok?"OK":"ERROR")<<" - "<<item.message<<"\r\n";for(const auto& item:runtimeItems)output<<item.name<<": "<<(item.ok?"OK":"ERROR")<<" - "<<item.message<<"\r\n";return output.str();}
struct DockerStatusResult{std::string text;};
void requestDockerStatus(HWND mainWindow){
    if(gClosing||!gDockerSettings||!gDockerSettings->handle())return;
    bool expected=false;if(!gDockerCheckRunning.compare_exchange_strong(expected,true))return;
    const auto state=snapshot();const auto target=gDockerSettings->handle();
    gWorker->submit([mainWindow,target,state](){
        auto result=std::make_unique<DockerStatusResult>();
        try{const auto stat=dependencies::checkStatic(state.settings,state.profile?&*state.profile:nullptr,true);const auto runtime=dependencies::checkRuntime(state.settings,gLlama->running(),true);result->text=dockerStatusText(stat,runtime);}
        catch(const std::exception& e){result->text=std::string("Docker status check failed: ")+e.what();}
        gDockerCheckRunning=false;PostMessageW(mainWindow,kDockerStatusResult,reinterpret_cast<WPARAM>(target),reinterpret_cast<LPARAM>(result.release()));
    });
}
std::string gib(std::uint64_t bytes){std::ostringstream value;value<<std::fixed<<std::setprecision(1)<<(static_cast<double>(bytes)/(1024.0*1024.0*1024.0));return value.str();}
std::string memoryValue(const std::optional<std::uint64_t>& bytes,const char* unavailable="-"){return bytes?gib(*bytes)+" GB":unavailable;}
void renderMemory(){if(!gMemory)return;std::ostringstream output;if(gMemorySnapshot.systemRamAvailable){const auto percent=gMemorySnapshot.totalRam?static_cast<unsigned>(gMemorySnapshot.usedRam*100/gMemorySnapshot.totalRam):0;output<<"RAM: "<<gib(gMemorySnapshot.usedRam)<<" / "<<gib(gMemorySnapshot.totalRam)<<" GB ("<<percent<<"%) | Free "<<gib(gMemorySnapshot.availableRam)<<" GB | AI "<<memoryValue(gMemorySnapshot.aiRam)<<"\r\n";}else output<<"RAM: unavailable | AI -\r\n";if(gMemorySnapshot.vramAvailable){const auto percent=gMemorySnapshot.totalVram?static_cast<unsigned>(gMemorySnapshot.usedVram*100/gMemorySnapshot.totalVram):0;output<<"VRAM: "<<gib(gMemorySnapshot.usedVram)<<" / "<<gib(gMemorySnapshot.totalVram)<<" GB ("<<percent<<"%) | Free "<<gib(gMemorySnapshot.availableVram)<<" GB | AI "<<memoryValue(gMemorySnapshot.aiVram,"n/a");}else output<<"VRAM: unavailable | AI unavailable";output<<" | Speed: "<<gMemorySnapshot.generationSpeed;SetWindowTextW(gMemory,wide(output.str()).c_str());}
void clearAiMemory(){gMemorySnapshot.aiRam.reset();gMemorySnapshot.aiVram.reset();gMemorySnapshot.generationSpeed="n/a";renderMemory();}
void requestMemory(HWND window){bool expected=false;if(!gMemoryCheckRunning.compare_exchange_strong(expected,true))return;const auto settings=gConfig->settings();gWorker->submit([window,settings](){auto result=std::make_unique<memory::Snapshot>(memory::collect(gProcess->processHandle(),gProcess->pid()));result->generationSpeed=gLlamaMetrics.sample(settings.host,settings.port,gProcess->pid());gMemoryCheckRunning=false;PostMessageW(window,kMemoryResult,0,reinterpret_cast<LPARAM>(result.release()));});}
void postOperation(HWND window,std::string message,bool processRunning=false){if(!message.empty()&&gLogs)gLogs->write(message);auto* result=new std::pair<std::string,bool>(std::move(message),processRunning);PostMessageW(window,kOperationResult,0,reinterpret_cast<LPARAM>(result));}
void connectDocker(HWND window){
    if(gOperationRunning.exchange(true)){queueLog("An operation is already running.");return;}
    const auto settings=gConfig->settings();
    gWorker->submit([window,settings](){
        if(!settings.dockerEnabled){postOperation(window,"Docker sandbox is disabled in Docker Settings.");gOperationRunning=false;return;}
        std::string error;
        if(!gSandbox->engineRunning()){
            if(!gSandbox->startDesktop(error)){postOperation(window,error);gOperationRunning=false;return;}
            if(gLogs)gLogs->write("Docker Desktop start requested; waiting for Docker Engine.");
        }
        for(int attempt=0;attempt<60&&!gSandbox->engineRunning();++attempt){
            if(gClosing){postOperation(window,"Docker readiness wait cancelled during shutdown.");gOperationRunning=false;return;}
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if(!gSandbox->engineRunning()){postOperation(window,"Docker Engine did not become ready within 60 seconds.");gOperationRunning=false;return;}
        if(!gSandbox->ensureSandbox(settings.workspace,error)){postOperation(window,error);gOperationRunning=false;return;}
        if(gLogs)gLogs->write("Docker sandbox connected for workspace "+util::pathText(settings.workspace)+".");
        postOperation(window,"Docker Engine and sandbox are connected.");
        gOperationRunning=false;
    });
}
void disconnectDocker(HWND window){
    if(gOperationRunning.exchange(true)){queueLog("An operation is already running.");return;}
    gWorker->submit([window](){
        if(gLlama->running()){postOperation(window,"Stop AI before disconnecting the Docker sandbox.");gOperationRunning=false;return;}
        std::string error;const bool ok=!gSandbox||gSandbox->removeSandbox(error);postOperation(window,ok?"Docker sandbox disconnected.":error);gOperationRunning=false;
    });
}
void setDockerEnabled(HWND window,bool enabled){
    gConfig->settings().dockerEnabled=enabled;persistConfig();
    if(enabled){queueLog("Docker sandbox enabled. Connect it from Docker Settings when needed.");requestDockerStatus(window);}
    else {queueLog("Docker sandbox disabled. AI will start without Docker tools.");disconnectDocker(window);}
}
void openDockerSettings(HWND window){
    if(!gDockerSettings){
        gDockerSettings=std::make_unique<ui::DockerSettingsWindow>(window,gConfig->settings().dockerEnabled,
            [window](HWND,bool enabled){setDockerEnabled(window,enabled);},
            [window](HWND){connectDocker(window);},
            [window](HWND){disconnectDocker(window);},
            [window](HWND){rebuildSandbox(window);},
            [window](HWND){std::error_code ec;std::filesystem::create_directories(gConfig->settings().workspace,ec);if(ec)MessageBoxW(window,wide("Cannot create workspace: "+util::pathText(gConfig->settings().workspace)).c_str(),L"Workspace",MB_OK|MB_ICONERROR);else openPath(gConfig->settings().workspace.wstring());},
            [window](HWND){changeWorkspace(window);},
            [window](HWND){requestDockerStatus(window);});
    }
    gDockerSettings->show();
    requestDockerStatus(window);
}
void startServer(HWND window){
    if(gOperationRunning.exchange(true)){queueLog("An operation is already running.");return;}
    const auto state=snapshot();
    gWorker->submit([window,state](){
        if(!state.profile){postOperation(window,"No model profile configured.");gOperationRunning=false;return;}
        std::error_code ec;std::filesystem::create_directories(state.settings.workspace,ec);
        const auto stat=dependencies::checkStatic(state.settings,&*state.profile,false);
        for(const auto& item:stat)if(!item.ok&&(item.name=="llama"||item.name=="Model"||item.name=="Workspace")){
            postOperation(window,item.message);gOperationRunning=false;return;
        }
        auto profile=*state.profile;
        const auto runtime=state.settings.dockerEnabled&&gSandbox?gSandbox->runtimeSpec():std::string{};
        if(runtime.empty()){profile.tools="none";profile.toolsRuntime="none";}
        else profile.toolsRuntime=runtime;
        if(port::isListening(state.settings.host,state.settings.port)){postOperation(window,"Configured server port is already in use.");gOperationRunning=false;return;}
        std::string error;const bool started=gLlama->start(state.settings,profile,error);
        postOperation(window,started?(runtime.empty()?"llama serve started without Docker tools.":"llama serve started with the connected Docker sandbox."):error,started);gOperationRunning=false;
    });
}
void stopServer(HWND window,bool restart){
    if(gOperationRunning.exchange(true)){queueLog("An operation is already running.");return;}
    clearAiMemory();const auto state=snapshot();
    gWorker->submit([window,state,restart](){
        gLlama->stop();
        if(restart&&state.profile){
            std::string error;const auto stat=dependencies::checkStatic(state.settings,&*state.profile,false);bool valid=true;
            for(const auto& item:stat)if(!item.ok&&(item.name=="llama"||item.name=="Model"||item.name=="Workspace")){error=item.message;valid=false;break;}
            if(valid&&port::isListening(state.settings.host,state.settings.port)){error="Configured server port is still in use.";valid=false;}
            if(valid){
                auto profile=*state.profile;const auto runtime=state.settings.dockerEnabled&&gSandbox?gSandbox->runtimeSpec():std::string{};
                if(runtime.empty()){profile.tools="none";profile.toolsRuntime="none";}else profile.toolsRuntime=runtime;
                const bool started=gLlama->start(state.settings,profile,error);postOperation(window,started?"llama serve restarted.":error,started);
            }else postOperation(window,error);
        }else postOperation(window,"llama serve stopped.");
        gOperationRunning=false;
    });
}
void rebuildSandbox(HWND window){if(gOperationRunning.exchange(true)){queueLog("An operation is already running.");return;}const auto settings=gConfig->settings();gWorker->submit([window,settings](){docker::DockerManager manager(settings.dockerImage);std::string error;const bool ok=manager.rebuild(appDir()/L"docker",error);postOperation(window,ok?"Docker sandbox rebuilt.":error);gOperationRunning=false;});}
void checkUpdates(HWND window){if(gOperationRunning.exchange(true)){queueLog("An operation is already running.");return;}gWorker->submit([window](){const auto result=update::UpdateManager::launchCheck();postOperation(window,result.message);gOperationRunning=false;});}
void changeWorkspace(HWND window){
    if(gProcessRunning){MessageBoxW(window,L"Stop AI before changing the Docker workspace.",L"Workspace",MB_OK|MB_ICONWARNING);return;}
    gPickerOpen=true;const auto path=ui::pickModelPath(window,true);gPickerOpen=false;
    if(!path||gClosing)return;
    const auto selected=std::filesystem::absolute(*path).lexically_normal();std::error_code ec;std::filesystem::create_directories(selected,ec);
    if(ec){MessageBoxW(window,wide("Cannot create workspace: "+util::pathText(selected)+" ("+ec.message()+")").c_str(),L"Workspace",MB_OK|MB_ICONERROR);return;}
    gConfig->settings().workspace=selected;persistConfig();queueLog("Workspace changed to: "+util::pathText(selected)+". Reconnect Docker to apply the new mount.");
    disconnectDocker(window);
    requestStatic(window);
}

config::Profile* mutableSelectedProfile(){
    auto& settings=gConfig->settings();
    for(auto& profile:settings.profiles)if(profile.id==settings.selectedProfile)return &profile;
    return nullptr;
}

struct ModelChange {config::Profile model;std::vector<config::Profile> discovered;std::filesystem::path folder;std::string locateId,error;bool folderScan=false;};
void addExisting(HWND window,bool locate){
    std::string id;if(locate){if(auto* p=mutableSelectedProfile())id=p->id;else return;}
    gPickerOpen=true;const auto path=ui::pickModelPath(window,false);gPickerOpen=false;
    if(!path||gClosing)return;
    gWorker->submit([window,path=*path,id]{
        auto result=std::make_unique<ModelChange>();result->locateId=id;
        try{result->model=models::fromExisting(path);}catch(const std::exception& e){result->error=e.what();}
        PostMessageW(window,kModelResult,0,reinterpret_cast<LPARAM>(result.release()));
    });
}
void browseModelFolder(HWND window){
    gPickerOpen=true;const auto folder=ui::pickModelPath(window,true);gPickerOpen=false;
    if(!folder||gClosing)return;
    gWorker->submit([window,folder=*folder]{
        auto result=std::make_unique<ModelChange>();result->folderScan=true;result->folder=folder;
        try{result->discovered=models::discoverInFolder(folder);}catch(const std::exception& e){result->error=e.what();}
        PostMessageW(window,kModelResult,0,reinterpret_cast<LPARAM>(result.release()));
    });
}
void removeModel(HWND window){
    const auto* p=gConfig->selectedProfile();if(!p)return;const auto id=p->id;
    const auto text=wide("Remove "+p->name+" from the launcher list?\nThe GGUF file will NOT be deleted. A running server is unaffected.");
    if(MessageBoxW(window,text.c_str(),L"Remove from list",MB_OKCANCEL|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDOK)return;
    auto& settings=gConfig->settings();std::erase_if(settings.profiles,[&](const auto& entry){return entry.id==id;});
    if(settings.selectedProfile==id)settings.selectedProfile=settings.profiles.empty()?"":settings.profiles.front().id;
    refreshModels();persistConfig();requestStatic(window);
}
bool handleMissing(HWND window){
    const auto* p=gConfig->selectedProfile();if(!p||!p->missing)return false;
    const auto text=L"Model file not found:\n"+p->model.wstring();
    const TASKDIALOG_BUTTON buttons[]={{100,L"Locate GGUF..."},{101,L"Remove from list (keep file on disk)"}};
    TASKDIALOGCONFIG config{sizeof(config)};config.hwndParent=window;config.pszWindowTitle=L"Missing model";config.pszMainInstruction=text.c_str();config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;config.dwCommonButtons=TDCBF_CANCEL_BUTTON;config.cButtons=2;config.pButtons=buttons;config.nDefaultButton=IDCANCEL;
    int choice=IDCANCEL;TaskDialogIndirect(&config,&choice,nullptr,nullptr);
    if(choice==100)addExisting(window,true);else if(choice==101)removeModel(window);return true;
}

void openModelSettings(HWND window){
    auto* profile=mutableSelectedProfile();
    if(!profile){MessageBoxW(window,L"No model profile configured.",L"Model Settings",MB_OK|MB_ICONWARNING);return;}
    std::string error;auto edited=*profile;
    gPickerOpen=true;
    const bool saved=ui::showModelSettings(window,edited,error);gPickerOpen=false;
    if(saved){
        for(auto& p:gConfig->settings().profiles)if(p.id==edited.id){p=edited;break;}
        persistConfig();
        queueLog("Model settings queued for save: "+edited.name+(gProcessRunning?". Restart AI to apply changes.":"."));
        requestStatic(window);
    }else if(!error.empty()){
        MessageBoxA(window,error.c_str(),"Model Settings",MB_OK|MB_ICONERROR);
    }
}

int buttonTextWidth(HWND button){wchar_t text[128]{};const int length=GetWindowTextW(button,text,static_cast<int>(std::size(text)));HDC dc=GetDC(button);if(!dc)return 105;const auto font=reinterpret_cast<HFONT>(SendMessageW(button,WM_GETFONT,0,0));const auto previous=font?SelectObject(dc,font):nullptr;SIZE size{};GetTextExtentPoint32W(dc,text,length,&size);if(previous)SelectObject(dc,previous);ReleaseDC(button,dc);return std::max(105,static_cast<int>(size.cx)+32);}
void layout(HWND window){RECT rect{};GetClientRect(window,&rect);const int width=std::max(640L,rect.right),height=std::max(480L,rect.bottom);constexpr int margin=20,buttonTop=20,buttonHeight=28,gap=5,rowGap=5,profileWidth=280,browseWidth=100;MoveWindow(gProfile,margin,buttonTop,profileWidth,240,TRUE);MoveWindow(gBrowseModels,margin+profileWidth+gap,buttonTop,browseWidth,buttonHeight,TRUE);const int toolbarLeft=width<1100?margin:margin+profileWidth+browseWidth+2*gap,toolbarRight=width-margin;int x=toolbarLeft,y=width<1100?58:buttonTop;for(HWND button:gButtons){const int buttonWidth=buttonTextWidth(button);if(x!=toolbarLeft&&x+buttonWidth>toolbarRight){x=toolbarLeft;y+=buttonHeight+rowGap;}MoveWindow(button,x,y,buttonWidth,buttonHeight,TRUE);x+=buttonWidth+gap;}const int contentTop=y+buttonHeight+12;const int statusHeight=std::clamp(height-contentTop-180,130,190);MoveWindow(gStatus,margin,contentTop,width-2*margin,statusHeight,TRUE);MoveWindow(gMemory,margin,contentTop+statusHeight+10,width-2*margin,48,TRUE);const int logTop=contentTop+statusHeight+70;MoveWindow(gLog,margin,logTop,width-2*margin,std::max(60,height-logTop-20),TRUE);}
void openPath(const std::wstring& path){ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}

LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    switch(message){
    case WM_CREATE:{
        gProfile=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kProfile)),nullptr,nullptr);
        gBrowseModels=CreateWindowW(L"BUTTON",L"Browse...",WS_CHILD|WS_VISIBLE,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBrowseModels)),nullptr,nullptr);
        gStatus=CreateWindowW(L"EDIT",L"Checking dependencies...",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_MULTILINE|ES_READONLY,0,0,0,0,window,nullptr,nullptr,nullptr);
        gMemory=CreateWindowW(L"STATIC",L"Memory: checking...",WS_CHILD|WS_VISIBLE|WS_BORDER|SS_LEFT,0,0,0,0,window,nullptr,nullptr,nullptr);
        gLog=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLog)),nullptr,nullptr);
        for(const auto& button:{std::pair{kStart,L"Start AI"},std::pair{kStop,L"Stop"},std::pair{kRestart,L"Restart"},std::pair{kWeb,L"Open Web UI"},std::pair{kModelSettings,L"Model Settings"},std::pair{kDockerSettings,L"Docker Settings"},std::pair{kDownload,L"Download Model"},std::pair{kAddExisting,L"Add Existing Model"},std::pair{kModels,L"Open Model Folder"},std::pair{kLocate,L"Locate Model"},std::pair{kRemove,L"Remove from List"},std::pair{kUpdate,L"Check Updates"}})
            gButtons.push_back(CreateWindowW(L"BUTTON",button.second,WS_CHILD|WS_VISIBLE,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(button.first)),nullptr,nullptr));
        refreshModels();
        gDownload=std::make_unique<ui::DownloadWindow>(window,[window](config::InstalledModel model,const std::filesystem::path& directory){
            models::registerModel(gConfig->settings(),std::move(model));gConfig->settings().lastModelDownloadDirectory=directory;
            refreshModels();persistConfig();requestStatic(window);
        },[](const std::string& text){queueLog(text);},[](const std::filesystem::path& folder){
            if(gConfig->settings().lastModelDownloadDirectory!=folder){gConfig->settings().lastModelDownloadDirectory=folder;persistConfig();}
        });
        gLogs->setListener([](const std::string& line){queueLog(line);});
        layout(window);requestStatic(window);requestRuntime(window);requestMemory(window);
        SetTimer(window,kStaticTimer,5000,nullptr);SetTimer(window,kRuntimeTimer,1000,nullptr);SetTimer(window,kLogTimer,200,nullptr);SetTimer(window,kMemoryTimer,1000,nullptr);
        queueLog(std::string("AI-Agent-LVK v")+AI_AGENT_LVK_VERSION+" ready.");
        return 0;
    }
    case WM_SIZE:layout(window);return 0;
    case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(lParam);info->ptMinTrackSize={700,590};return 0;}
    case WM_TIMER:if(wParam==kCloseTimer){PostMessageW(window,WM_CLOSE,0,0);return 0;}if(gClosing)return 0;if(wParam==kStaticTimer)requestStatic(window);else if(wParam==kRuntimeTimer)requestRuntime(window);else if(wParam==kLogTimer)flushLog();else if(wParam==kMemoryTimer)requestMemory(window);return 0;
    case kStaticResult:{std::unique_ptr<StaticResult> result(reinterpret_cast<StaticResult*>(lParam));gStaticItems=std::move(result->items);bool changed=false;
        for(const auto& checked:result->models)for(auto& p:gConfig->settings().profiles)if(p.id==checked.id&&p.model==checked.model){changed|=p.missing!=checked.missing;p.missing=checked.missing;if(p.mtpModelPath==checked.mtpModelPath)p.mtpFileAvailable=checked.mtpFileAvailable;}
        if(changed)refreshModels();renderStatus();return 0;}
    case kModelResult:{std::unique_ptr<ModelChange> result(reinterpret_cast<ModelChange*>(lParam));
        if(!result->error.empty()){queueLog(result->error);if(!gClosing)MessageBoxW(window,wide(result->error).c_str(),L"Model registration",MB_OK|MB_ICONERROR);return 0;}
        if(result->folderScan){
            auto& settings=gConfig->settings();const auto selected=settings.selectedProfile;const auto firstDiscovered=result->discovered.empty()?std::filesystem::path{}:result->discovered.front().model;size_t added=0;
            for(auto& model:result->discovered){
                const auto existing=std::find_if(settings.profiles.begin(),settings.profiles.end(),[&model](const auto& profile){return _wcsicmp(profile.model.c_str(),model.model.c_str())==0;});
                if(existing==settings.profiles.end()){settings.profiles.push_back(std::move(model));++added;}
                else existing->missing=false;
            }
            const auto stillSelected=std::find_if(settings.profiles.begin(),settings.profiles.end(),[&selected](const auto& profile){return profile.id==selected;});
            if(stillSelected==settings.profiles.end()&&!firstDiscovered.empty()){
                const auto found=std::find_if(settings.profiles.begin(),settings.profiles.end(),[&firstDiscovered](const auto& profile){return _wcsicmp(profile.model.c_str(),firstDiscovered.c_str())==0;});
                if(found!=settings.profiles.end())settings.selectedProfile=found->id;
            }
            settings.lastModelBrowseDirectory=result->folder;
            queueLog("Scanned model folder: "+util::pathText(result->folder)+". Found "+std::to_string(result->discovered.size())+" GGUF file(s); added "+std::to_string(added)+" new model(s).");
        }else if(result->locateId.empty())models::registerModel(gConfig->settings(),std::move(result->model));
        else for(auto& p:gConfig->settings().profiles)if(p.id==result->locateId){p.model=result->model.model;p.missing=false;break;}
        refreshModels();persistConfig();requestStatic(window);return 0;}
    case kRuntimeResult:{std::unique_ptr<std::pair<std::vector<dependencies::Item>,bool>> result(reinterpret_cast<std::pair<std::vector<dependencies::Item>,bool>*>(lParam));gRuntimeItems=std::move(result->first);gProcessRunning=result->second;renderStatus();return 0;}
    case kDockerStatusResult:{std::unique_ptr<DockerStatusResult> result(reinterpret_cast<DockerStatusResult*>(lParam));if(gDockerSettings&&gDockerSettings->handle()==reinterpret_cast<HWND>(wParam))gDockerSettings->setStatus(result->text);return 0;}
    case kMemoryResult:{std::unique_ptr<memory::Snapshot> result(reinterpret_cast<memory::Snapshot*>(lParam));gMemorySnapshot=std::move(*result);if(!gMemorySnapshot.vramAvailable){if(!gVramMessageLogged&&!gMemorySnapshot.vramMessage.empty()){const auto vramMessage=gMemorySnapshot.vramMessage;gWorker->submit([vramMessage](){if(gLogs)gLogs->write(vramMessage);});gVramMessageLogged=true;}}else gVramMessageLogged=false;renderMemory();return 0;}
    case kOperationResult:{std::unique_ptr<std::pair<std::string,bool>> result(reinterpret_cast<std::pair<std::string,bool>*>(lParam));gProcessRunning=result->second;if(gDockerSettings&&gDockerSettings->handle())requestDockerStatus(window);if(!gClosing){requestStatic(window);requestRuntime(window);requestMemory(window);}return 0;}
    case WM_COMMAND:
        if(gClosing)return 0;
        if(HIWORD(wParam)==CBN_SELCHANGE&&LOWORD(wParam)==kProfile){const int index=(int)SendMessageW(gProfile,CB_GETCURSEL,0,0);if(index>=0&&static_cast<size_t>(index)<gConfig->settings().profiles.size()){gConfig->settings().selectedProfile=gConfig->settings().profiles[static_cast<size_t>(index)].id;persistConfig();requestStatic(window);if(gProcessRunning)queueLog("Active model changed. Restart AI to load the selected model.");}return 0;}
        if(HIWORD(wParam)!=BN_CLICKED)break;
        switch(LOWORD(wParam)){
        case kStart:if(!handleMissing(window))startServer(window);break;
        case kBrowseModels:browseModelFolder(window);break;
        case kDockerSettings:openDockerSettings(window);break;
        case kStop:stopServer(window,false);break;
        case kRestart:if(!handleMissing(window))stopServer(window,true);break;
        case kDownload:gDownload->show(gConfig->settings().lastModelDownloadDirectory);break;
        case kAddExisting:addExisting(window,false);break;
        case kLocate:addExisting(window,true);break;
        case kRemove:removeModel(window);break;
        case kWeb:{const auto url=L"http://"+wide(gConfig->settings().host)+L":"+std::to_wstring(gConfig->settings().port);openPath(url);break;}
        case kModelSettings:openModelSettings(window);break;
        case kModels:if(const auto* p=gConfig->selectedProfile())openPath(p->model.parent_path().wstring());break;
        case kUpdate:checkUpdates(window);break;
        default:break;
        }
        return 0;
    case WM_CLOSE:
        gClosing=true;if(gPickerOpen){SetTimer(window,kCloseTimer,200,nullptr);return 0;}
        if(gDockerSettings)gDockerSettings->close();gDownload->cancelAndClose();if(gDownload->running()){SetTimer(window,kCloseTimer,200,nullptr);return 0;}
        if(!gShutdownQueued){gShutdownQueued=true;KillTimer(window,kCloseTimer);SetWindowTextW(window,L"AI-Agent-LVK - finishing background operations...");
            gWorker->submit([window]{gLlama->stop();if(gSandbox){std::string error;if(!gSandbox->removeSandbox(error)&&gLogs)gLogs->write(error);}PostMessageW(window,kShutdownComplete,0,0);});}
        return 0;
    case kShutdownComplete:DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,kStaticTimer);KillTimer(window,kRuntimeTimer);KillTimer(window,kLogTimer);KillTimer(window,kMemoryTimer);KillTimer(window,kCloseTimer);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    if(FAILED(com)){MessageBoxW(nullptr,L"Cannot initialize Windows file dialogs.",L"Startup error",MB_ICONERROR);return 1;}
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_PROGRESS_CLASS|ICC_BAR_CLASSES};InitCommonControlsEx(&common);
    gConfig=std::make_unique<lvk::config::ConfigManager>(appDir());
    std::string error;
    if(!gConfig->load(error)){MessageBoxW(nullptr,wide(error).c_str(),L"Configuration error - original file preserved",MB_ICONERROR);CoUninitialize();return 1;}
    gLogs=std::make_unique<lvk::log::LogManager>(appDir()/L"logs");
    gProcess=std::make_unique<lvk::process::ProcessManager>();
    gLlama=std::make_unique<lvk::launcher::LlamaManager>(*gProcess,*gLogs);
    gSandbox=std::make_unique<lvk::docker::DockerManager>(gConfig->settings().dockerImage);
    gBridge=std::make_unique<lvk::update::UpdateCloseBridge>();
    gWorker=std::make_unique<lvk::process::BackgroundWorker>();
    gBridge->start();
    WNDCLASSW klass{};klass.lpfnWndProc=windowProc;klass.hInstance=instance;klass.lpszClassName=L"AI-Agent-LVK-Window";klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&klass);
    HWND window=CreateWindowW(klass.lpszClassName,L"AI-Agent-LVK - llama.cpp launcher",WS_OVERLAPPEDWINDOW,100,100,1320,580,nullptr,nullptr,instance,nullptr);
    if(!window)return 1;
    ShowWindow(window,show);UpdateWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    gDockerSettings.reset();gDownload.reset();gSandbox.reset();gBridge->stop();gWorker.reset();CoUninitialize();return 0;
}
