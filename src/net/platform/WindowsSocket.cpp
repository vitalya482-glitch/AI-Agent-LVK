#include "net/PlatformSocket.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <limits>

namespace lvk::net::platform {
namespace {

SOCKET nativeSocket(SocketHandle handle) {
    return static_cast<SOCKET>(handle);
}

SocketHandle portableSocket(SOCKET handle) {
    if (handle == INVALID_SOCKET) {
        return kInvalidSocket;
    }
    return static_cast<SocketHandle>(handle);
}

} // namespace

bool initialize() {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void cleanup() {
    WSACleanup();
}

SocketHandle createListener(const std::string& host, std::uint16_t port) {
    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return kInvalidSocket;
    }

    const int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::closesocket(listener);
        return kInvalidSocket;
    }

    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        ::closesocket(listener);
        return kInvalidSocket;
    }

    if (::listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        ::closesocket(listener);
        return kInvalidSocket;
    }

    return portableSocket(listener);
}

SocketHandle acceptClient(SocketHandle listener) {
    return portableSocket(::accept(nativeSocket(listener), nullptr, nullptr));
}

int receive(SocketHandle socket, char* buffer, int length) {
    return ::recv(nativeSocket(socket), buffer, length, 0);
}

bool sendAll(SocketHandle socket, const char* data, std::size_t length) {
    std::size_t sent = 0;
    while (sent < length) {
        const std::size_t remaining = length - sent;
        const int chunkSize = static_cast<int>(std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<int>::max())));

        const int result = ::send(nativeSocket(socket), data + sent, chunkSize, 0);
        if (result == SOCKET_ERROR || result == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

void closeSocket(SocketHandle socket) {
    if (socket == kInvalidSocket) {
        return;
    }

    const SOCKET native = nativeSocket(socket);
    ::shutdown(native, SD_BOTH);
    ::closesocket(native);
}

} // namespace lvk::net::platform
