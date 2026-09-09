#pragma once
#include <string>
namespace lvk::port { bool isFree(const std::string& host, unsigned short port); bool isListening(const std::string& host, unsigned short port); }
