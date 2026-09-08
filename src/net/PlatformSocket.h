#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lvk::net::platform {

using SocketHandle = std::intptr_t;
inline constexpr SocketHandle kInvalidSocket = static_cast<SocketHandle>(-1);

bool initialize();
void cleanup();

SocketHandle createListener(const std::string& host, std::uint16_t port);
SocketHandle acceptClient(SocketHandle listener);
int receive(SocketHandle socket, char* buffer, int length);
bool sendAll(SocketHandle socket, const char* data, std::size_t length);
void closeSocket(SocketHandle socket);

} // namespace lvk::net::platform
