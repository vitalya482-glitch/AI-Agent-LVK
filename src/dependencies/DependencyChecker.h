#pragma once
#include "config/ConfigManager.h"
#include <string>
#include <vector>
namespace lvk::dependencies { struct Item{std::string name,message;bool ok=false;}; std::vector<Item> checkStatic(const config::Settings& s,const config::Profile* p,bool includeDocker=true); std::vector<Item> checkRuntime(const config::Settings& s,bool llamaProcessRunning,bool includeDocker=true); }
