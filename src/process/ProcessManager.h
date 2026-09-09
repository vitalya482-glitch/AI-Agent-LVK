#pragma once
#include <atomic>
#include <string>
#include <thread>
#include <functional>
#include <windows.h>
namespace lvk::process { class ProcessManager { public: ~ProcessManager(); bool start(const std::wstring& exe,const std::wstring& args,const std::wstring& cwd,std::function<void(const std::string&)> output); bool stop(); bool running() const; unsigned long pid() const; HANDLE processHandle() const noexcept; private: void read(HANDLE pipe,std::function<void(const std::string&)> output); HANDLE process_=nullptr, outRead_=nullptr; std::thread reader_; std::atomic<bool> running_{false}; unsigned long pid_=0; }; }
