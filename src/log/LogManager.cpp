#include "log/LogManager.h"
#include <fstream>
#include <chrono>
#include <utility>
#include <iterator>
namespace lvk::log { LogManager::LogManager(std::filesystem::path d):file_(d/L"launcher.log"),llamaFile_(d/L"llama.log"){std::filesystem::create_directories(file_.parent_path());} void LogManager::write(const std::string& line){std::lock_guard l(mutex_);std::ofstream f(file_,std::ios::app);std::ofstream llama(llamaFile_,std::ios::app);f<<line<<'\n';llama<<line<<'\n';if(std::filesystem::file_size(file_)>4*1024*1024){f.close();std::ifstream in(file_);std::string content((std::istreambuf_iterator<char>(in)),{});in.close();if(content.size()>2*1024*1024){std::ofstream trim(file_,std::ios::trunc);trim.write(content.data()+content.size()-2*1024*1024,2*1024*1024);}}if(listener_)listener_(line);}void LogManager::setListener(std::function<void(const std::string&)> l){std::lock_guard g(mutex_);listener_=std::move(l);} }
