#include "api/ApiServer.h"

#include "core/AppConfig.h"
#include "core/CommandDispatcher.h"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

namespace lvk::api {
namespace {

std::string jsonEscape(std::string_view value) {
    std::ostringstream output;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20) {
                output << "\\u"
                       << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(ch)
                       << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(ch);
            }
            break;
        }
    }
    return output.str();
}

bool extractJsonString(const std::string& json, std::string_view field, std::string& value) {
    const std::string token = "\"" + std::string(field) + "\"";
    const auto fieldPos = json.find(token);
    if (fieldPos == std::string::npos) {
        return false;
    }

    const auto colon = json.find(':', fieldPos + token.size());
    if (colon == std::string::npos) {
        return false;
    }

    auto pos = colon + 1;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }

    if (pos >= json.size() || json[pos] != '"') {
        return false;
    }
    ++pos;

    std::string result;
    while (pos < json.size()) {
        const char ch = json[pos++];
        if (ch == '"') {
            value = std::move(result);
            return true;
        }

        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }

        if (pos >= json.size()) {
            return false;
        }

        const char escaped = json[pos++];
        switch (escaped) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: return false;
        }
    }

    return false;
}

net::HttpResponse jsonResponse(int statusCode, std::string body) {
    return {statusCode, "application/json; charset=utf-8", std::move(body)};
}

} // namespace

ApiServer::ApiServer(core::CommandDispatcher& dispatcher, std::string host, std::uint16_t port)
    : dispatcher_(dispatcher), host_(std::move(host)), port_(port) {
}

bool ApiServer::start() {
    return server_.start(host_, port_, [this](const net::HttpRequest& request) {
        return handle(request);
    });
}

void ApiServer::stop() {
    server_.stop();
}

bool ApiServer::running() const noexcept {
    return server_.running();
}

net::HttpResponse ApiServer::handle(const net::HttpRequest& request) const {
    if (request.method == "GET" && request.path == "/api/v1/status") {
        std::ostringstream body;
        body
            << "{\"ok\":true"
            << ",\"core\":\"running\""
            << ",\"apiVersion\":" << core::kApiVersion
            << ",\"host\":\"" << jsonEscape(host_) << "\""
            << ",\"port\":" << port_
            << ",\"model\":\"not_loaded\""
            << ",\"agents\":0"
            << '}';
        return jsonResponse(200, body.str());
    }

    if (request.method == "GET" && request.path == "/api/v1/version") {
        return jsonResponse(
            200,
            "{\"ok\":true,\"version\":\"" + jsonEscape(dispatcher_.version()) +
            "\",\"apiVersion\":" + std::to_string(core::kApiVersion) + "}");
    }

    if (request.method == "POST" && request.path == "/api/v1/command") {
        std::string command;
        if (!extractJsonString(request.body, "command", command)) {
            return jsonResponse(400, "{\"ok\":false,\"error\":\"missing_command\"}");
        }

        const auto result = dispatcher_.execute(command);
        return jsonResponse(
            result.ok ? 200 : 400,
            std::string("{\"ok\":") + (result.ok ? "true" : "false") +
            ",\"result\":\"" + jsonEscape(result.output) + "\"}");
    }

    if (request.method == "POST" && request.path == "/api/v1/chat") {
        std::string message;
        if (!extractJsonString(request.body, "message", message)) {
            return jsonResponse(400, "{\"ok\":false,\"error\":\"missing_message\"}");
        }

        return jsonResponse(
            503,
            "{\"ok\":false,\"error\":\"model_not_loaded\",\"message\":\"Chat endpoint is reserved for the model runtime.\"}");
    }

    const bool knownPath =
        request.path == "/api/v1/status" ||
        request.path == "/api/v1/version" ||
        request.path == "/api/v1/command" ||
        request.path == "/api/v1/chat";

    if (knownPath) {
        return jsonResponse(405, "{\"ok\":false,\"error\":\"method_not_allowed\"}");
    }

    return jsonResponse(404, "{\"ok\":false,\"error\":\"not_found\"}");
}

} // namespace lvk::api
