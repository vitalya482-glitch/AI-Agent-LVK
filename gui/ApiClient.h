#pragma once

#include <cstdint>
#include <string>

namespace lvk::gui {

struct ApiResponse {
    bool transportOk = false;
    int statusCode = 0;
    std::string body;
    std::string error;
};

class ApiClient {
public:
    ApiClient(std::string host, std::uint16_t port);

    ApiResponse get(const std::string& path) const;
    ApiResponse postJson(const std::string& path, const std::string& body) const;

private:
    ApiResponse request(
        const wchar_t* method,
        const std::string& path,
        const std::string& body) const;

    std::string host_;
    std::uint16_t port_ = 0;
};

} // namespace lvk::gui
