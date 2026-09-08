#pragma once

#include "net/HttpServer.h"

#include <cstdint>
#include <string>

namespace lvk::core {
class CommandDispatcher;
}

namespace lvk::api {

class ApiServer {
public:
    ApiServer(core::CommandDispatcher& dispatcher, std::string host, std::uint16_t port);

    bool start();
    void stop();
    bool running() const noexcept;

private:
    net::HttpResponse handle(const net::HttpRequest& request) const;

    core::CommandDispatcher& dispatcher_;
    std::string host_;
    std::uint16_t port_ = 0;
    net::HttpServer server_;
};

} // namespace lvk::api
