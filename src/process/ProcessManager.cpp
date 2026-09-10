#include "process/ProcessManager.h"
#include "util/Text.h"
#include <vector>
#include <utility>

namespace lvk::process {
ProcessManager::~ProcessManager(){stop();}
// Control methods are serialized by BackgroundWorker; the reader owns no process state.
bool ProcessManager::start(const std::wstring& exe,const std::wstring& args,const std::wstring& cwd,std::function<void(const std::string&)> callback){
    if(running())return false;
    stop(); // Reap a previously exited child before starting a different model.
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};HANDLE writePipe{};
    if(!CreatePipe(&outRead_,&writePipe,&attributes,0))return false;
    SetHandleInformation(outRead_,HANDLE_FLAG_INHERIT,0);
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ,&attributes,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(input==INVALID_HANDLE_VALUE){CloseHandle(writePipe);CloseHandle(outRead_);outRead_=nullptr;return false;}
    const auto line=util::quote(exe)+L" "+args;
    std::vector<wchar_t> command(line.begin(),line.end());command.push_back(0);
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;startup.wShowWindow=SW_HIDE;
    startup.hStdInput=input;startup.hStdOutput=writePipe;startup.hStdError=writePipe;
    PROCESS_INFORMATION info{};
    const bool created=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_NEW_PROCESS_GROUP,nullptr,cwd.empty()?nullptr:cwd.c_str(),&startup,&info)!=FALSE;
    CloseHandle(input);CloseHandle(writePipe);
    if(!created){CloseHandle(outRead_);outRead_=nullptr;return false;}
    CloseHandle(info.hThread);process_=info.hProcess;pid_=info.dwProcessId;running_=true;
    reader_=std::thread(&ProcessManager::read,this,outRead_,std::move(callback));return true;
}
void ProcessManager::read(HANDLE pipe,std::function<void(const std::string&)> callback){
    char buffer[4096];DWORD read{};
    while(ReadFile(pipe,buffer,sizeof(buffer),&read,nullptr)&&read)callback(std::string(buffer,read));
}
bool ProcessManager::stop(){
    if(process_){
        if(running()){
            GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,pid_);
            if(WaitForSingleObject(process_,3000)==WAIT_TIMEOUT)TerminateProcess(process_,1);
            WaitForSingleObject(process_,3000);
        }
        CloseHandle(process_);process_=nullptr;
    }
    running_=false;pid_=0;
    if(reader_.joinable()){
        // A tool child may have inherited the pipe; don't wait forever for it.
        if(WaitForSingleObject(reader_.native_handle(),1000)==WAIT_TIMEOUT)CancelSynchronousIo(reader_.native_handle());
        reader_.join();
    }
    if(outRead_){CloseHandle(outRead_);outRead_=nullptr;}
    return true;
}
bool ProcessManager::running()const{return process_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT;}
unsigned long ProcessManager::pid()const{return running()?pid_:0;}
HANDLE ProcessManager::processHandle()const noexcept{return process_;}
}
