#pragma once
#include "config/ConfigManager.h"
#include "process/ProcessManager.h"
#include "log/LogManager.h"
#include <string>
namespace lvk::launcher { class LlamaManager { public: LlamaManager(process::ProcessManager& p,log::LogManager& l):process_(p),log_(l){} bool start(const config::Settings&,const config::Profile&,std::string&); bool stop(); bool running()const; std::wstring commandLine(const config::Settings&,const config::Profile&)const; private: process::ProcessManager& process_;log::LogManager& log_; }; }
