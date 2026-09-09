#include "port/PortChecker.h"
#include <winsock2.h>
#include <ws2tcpip.h>
namespace lvk::port {
namespace { bool probe(const std::string& host, unsigned short port, bool connecting) { WSADATA data{}; WSAStartup(MAKEWORD(2,2), &data); SOCKET socketHandle=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(port); inet_pton(AF_INET,host.c_str(),&address.sin_addr); const int result=connecting ? ::connect(socketHandle,reinterpret_cast<sockaddr*>(&address),sizeof(address)) : ::bind(socketHandle,reinterpret_cast<sockaddr*>(&address),sizeof(address)); closesocket(socketHandle); WSACleanup(); return result==0; } }
bool isListening(const std::string& host,unsigned short port){return probe(host,port,true);}
bool isFree(const std::string& host,unsigned short port){return !probe(host,port,false);}
}
