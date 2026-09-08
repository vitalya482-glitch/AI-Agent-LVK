#include "net/HttpServer.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <sstream>
#include <string_view>
#include <utility>

namespace lvk::net {
namespace {

constexpr std::size_t kMaxHeaderBytes = 64 * 1024;
constexpr std::size_t kMaxBodyBytes = 1024 * 1024;

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool readRequest(platform::SocketHandle client, HttpRequest& request) {
    std::string data;
    data.reserve(4096);

    char buffer[4096];
    std::size_t headerEnd = std::string::npos;

    while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos) {
        if (data.size() >= kMaxHeaderBytes) {
            return false;
        }

        const int received = platform::receive(client, buffer, static_cast<int>(sizeof(buffer)));
        if (received <= 0) {
            return false;
        }
        data.append(buffer, static_cast<std::size_t>(received));
    }

    const std::string headerBlock = data.substr(0, headerEnd);
    std::istringstream headers(headerBlock);

    std::string requestLine;
    if (!std::getline(headers, requestLine)) {
        return false;
    }
    if (!requestLine.empty() && requestLine.back() == '\r') {
        requestLine.pop_back();
    }

    std::string httpVersion;
    std::istringstream requestLineStream(requestLine);
    if (!(requestLineStream >> request.method >> request.path >> httpVersion)) {
        return false;
    }

    std::size_t contentLength = 0;
    std::string line;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        std::string name = lowerCopy(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        const auto first = value.find_first_not_of(" \t");
        if (first != std::string::npos) {
            value.erase(0, first);
        } else {
            value.clear();
        }

        if (name == "content-length") {
            try {
                contentLength = static_cast<std::size_t>(std::stoull(value));
            } catch (...) {
                return false;
            }
        }
    }

    if (contentLength > kMaxBodyBytes) {
        return false;
    }

    const std::size_t bodyStart = headerEnd + 4;
    while (data.size() - bodyStart < contentLength) {
        const int received = platform::receive(client, buffer, static_cast<int>(sizeof(buffer)));
        if (received <= 0) {
            return false;
        }
        data.append(buffer, static_cast<std::size_t>(received));

        if (data.size() - bodyStart > kMaxBodyBytes) {
            return false;
        }
    }

    request.body = data.substr(bodyStart, contentLength);
    return true;
}

std::string reasonPhrase(int statusCode) {
    switch (statusCode) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "Response";
    }
}

HttpResponse badRequest() {
    return {400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"bad_request\"}"};
}

} // namespace

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start(const std::string& host, std::uint16_t port, Handler handler) {
    if (running_) {
        return false;
    }

    if (!platform::initialize()) {
        return false;
    }

    const auto listener = platform::createListener(host, port);
    if (listener == platform::kInvalidSocket) {
        platform::cleanup();
        return false;
    }

    handler_ = std::move(handler);
    listener_ = listener;
    running_ = true;

    try {
        thread_ = std::thread(&HttpServer::run, this);
    } catch (...) {
        running_ = false;
        const auto toClose = listener_.exchange(platform::kInvalidSocket);
        platform::closeSocket(toClose);
        platform::cleanup();
        handler_ = {};
        return false;
    }

    return true;
}

void HttpServer::stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }

    const auto listener = listener_.exchange(platform::kInvalidSocket);
    platform::closeSocket(listener);

    if (thread_.joinable()) {
        thread_.join();
    }

    handler_ = {};
    platform::cleanup();
}

bool HttpServer::running() const noexcept {
    return running_.load();
}

void HttpServer::run() {
    while (running_) {
        const auto listener = listener_.load();
        if (listener == platform::kInvalidSocket) {
            break;
        }

        const auto client = platform::acceptClient(listener);
        if (client == platform::kInvalidSocket) {
            if (!running_) {
                break;
            }
            continue;
        }

        handleClient(client);
        platform::closeSocket(client);
    }
}

void HttpServer::handleClient(platform::SocketHandle client) {
    HttpRequest request;
    HttpResponse response;

    if (!readRequest(client, request)) {
        response = badRequest();
    } else {
        try {
            response = handler_ ? handler_(request) : HttpResponse{500, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"no_handler\"}"};
        } catch (const std::exception&) {
            response = {500, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"handler_exception\"}"};
        } catch (...) {
            response = {500, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"handler_exception\"}"};
        }
    }

    std::ostringstream output;
    output
        << "HTTP/1.1 " << response.statusCode << ' ' << reasonPhrase(response.statusCode) << "\r\n"
        << "Content-Type: " << response.contentType << "\r\n"
        << "Content-Length: " << response.body.size() << "\r\n"
        << "Connection: close\r\n"
        << "Cache-Control: no-store\r\n"
        << "\r\n"
        << response.body;

    const std::string payload = output.str();
    platform::sendAll(client, payload.data(), payload.size());
}

} // namespace lvk::net
