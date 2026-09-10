#include "process/ProcessRunner.h"
#include "util/Text.h"
#include <windows.h>
#include <iterator>
#include <thread>
#include <vector>

namespace lvk::process {
namespace {
std::wstring quote(const std::wstring& value) {
    return util::quote(value);
}
void readPipe(HANDLE pipe,std::string& output){char buffer[4096];DWORD read=0;while(ReadFile(pipe,buffer,sizeof(buffer),&read,nullptr)&&read)output.append(buffer,read);CloseHandle(pipe);}
}
std::filesystem::path findOnPath(const std::wstring& executable){wchar_t buffer[32768]{};const DWORD length=SearchPathW(nullptr,executable.c_str(),L".exe",static_cast<DWORD>(std::size(buffer)),buffer,nullptr);return length==0||length>=std::size(buffer)?std::filesystem::path{}:std::filesystem::path(std::wstring(buffer,length));}
RunResult runHidden(const std::filesystem::path& executable,const std::wstring& arguments,const std::filesystem::path& workingDirectory){RunResult result;SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};HANDLE outputRead=nullptr,outputWrite=nullptr;if(!CreatePipe(&outputRead,&outputWrite,&attributes,0)){result.error="CreatePipe failed: "+std::to_string(GetLastError());return result;}SetHandleInformation(outputRead,HANDLE_FLAG_INHERIT,0);HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ,&attributes,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(input==INVALID_HANDLE_VALUE){CloseHandle(outputRead);CloseHandle(outputWrite);result.error="Could not create hidden stdin.";return result;}std::wstring command=quote(executable.wstring());if(!arguments.empty()){command+=L" ";command+=arguments;}std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(L'\0');STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;startup.wShowWindow=SW_HIDE;startup.hStdInput=input;startup.hStdOutput=outputWrite;startup.hStdError=outputWrite;PROCESS_INFORMATION information{};const BOOL created=CreateProcessW(executable.c_str(),mutableCommand.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,nullptr,workingDirectory.empty()?nullptr:workingDirectory.c_str(),&startup,&information);CloseHandle(input);CloseHandle(outputWrite);if(!created){CloseHandle(outputRead);result.error="CreateProcessW failed: "+std::to_string(GetLastError());return result;}result.started=true;std::thread reader(readPipe,outputRead,std::ref(result.output));WaitForSingleObject(information.hProcess,INFINITE);DWORD exitCode=1;GetExitCodeProcess(information.hProcess,&exitCode);reader.join();CloseHandle(information.hThread);CloseHandle(information.hProcess);result.exitCode=exitCode;return result;}
bool startHidden(const std::filesystem::path& executable,const std::wstring& arguments,const std::filesystem::path& workingDirectory,std::string& error){
    std::wstring command=quote(executable.wstring());
    if(!arguments.empty()){command+=L" ";command+=arguments;}
    std::vector<wchar_t> mutableCommand(command.begin(),command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags=STARTF_USESHOWWINDOW;
    startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION information{};
    const BOOL created=CreateProcessW(executable.c_str(),mutableCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,nullptr,workingDirectory.empty()?nullptr:workingDirectory.c_str(),&startup,&information);
    if(!created){error="CreateProcessW failed: "+std::to_string(GetLastError());return false;}
    CloseHandle(information.hThread);
    CloseHandle(information.hProcess);
    return true;
}
}
