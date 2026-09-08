#include "net/PlatformSocket.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <limits>

namespace lvk::net::platform {

bool initialize() {
    return true;
}

void cleanup() {
}

SocketHandle createListener(const std::string& host, std::uint16_t port) {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        return kInvalidSocket;
    }

    const int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::close(listener);
        return kInvalidSocket;
    }

    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(listener);
        return kInvalidSocket;
    }

    if (::listen(listener, SOMAXCONN) != 0) {
        ::close(listener);
        return kInvalidSocket;
    }

    return static_cast<SocketHandle>(listener);
}

SocketHandle acceptClient(SocketHandle listener) {
    const int client = ::accept(static_cast<int>(listener), nullptr, nullptr);
    return client < 0 ? kInvalidSocket : static_cast<SocketHandle>(client);
}

int receive(SocketHandle socket, char* buffer, int length) {
    const ssize_t result = ::recv(static_cast<int>(socket), buffer, static_cast<std::size_t>(length), 0);
    if (result < 0) {
        return -1;
    }
    return static_cast<int>(std::min<ssize_t>(result, std::numeric_limits<int>::max()));
}

bool sendAll(SocketHandle socket, const char* data, std::size_t length) {
    std::size_t sent = 0;
    while (sent < length) {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const ssize_t result = ::send(static_cast<int>(socket), data + sent, length - sent, flags);
        if (result <= 0) {
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

    const int native = static_cast<int>(socket);
    ::shutdown(native, SHUT_RDWR);
    ::close(native);
}

} // namespace lvk::net::platform
