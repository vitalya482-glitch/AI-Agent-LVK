#pragma once

#include "net/PlatformSocket.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace lvk::net {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

struct HttpResponse {
    int statusCode = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer() = default;
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool start(const std::string& host, std::uint16_t port, Handler handler);
    void stop();
    bool running() const noexcept;

private:
    void run();
    void handleClient(platform::SocketHandle client);

    std::atomic<bool> running_{false};
    std::atomic<platform::SocketHandle> listener_{platform::kInvalidSocket};
    std::thread thread_;
    Handler handler_;
};

} // namespace lvk::net
