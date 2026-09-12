#include "metrics/LlamaMetrics.h"
#include "util/Text.h"
#include <windows.h>
#include <winhttp.h>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace lvk::metrics {
namespace {
struct WinHttpHandle {
    HINTERNET value{};
    ~WinHttpHandle() { if (value) WinHttpCloseHandle(value); }
    operator HINTERNET() const noexcept { return value; }
};

std::optional<std::string> getMetrics(const std::string& host, unsigned short port) {
    std::wstring wideHost;
    try { wideHost = util::wide(host); } catch (...) { return std::nullopt; }
    WinHttpHandle session{WinHttpOpen(L"AI-Agent-LVK metrics/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return std::nullopt;
    WinHttpSetTimeouts(session, 250, 250, 500, 500);
    WinHttpHandle connection{WinHttpConnect(session, wideHost.c_str(), port, 0)};
    if (!connection.value) return std::nullopt;
    WinHttpHandle request{WinHttpOpenRequest(connection, L"GET", L"/metrics", nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_REFRESH)};
    if (!request.value || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) return std::nullopt;

    DWORD status = 0, statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) || status != 200) return std::nullopt;

    std::string body;
    body.reserve(8192);
    char buffer[8192];
    for (;;) {
        DWORD received = 0;
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &received)) return std::nullopt;
        if (!received) break;
        if (body.size() + received > 256 * 1024) return std::nullopt;
        body.append(buffer, received);
    }
    return body;
}

bool metricNameMatches(std::string_view name, std::string_view suffix) {
    return name == "llamacpp:" + std::string(suffix) || name == "llamacpp_" + std::string(suffix);
}

std::optional<double> metricValue(std::string_view text) {
    std::string value(text);
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) return std::nullopt;
    return parsed;
}

std::string normalizeMetricsBody(std::string_view body) {
    if (body.size() < 2 || body.front() != '"' || body.back() != '"') return std::string(body);
    std::string result;
    result.reserve(body.size() - 2);
    for (size_t i = 1; i + 1 < body.size(); ++i) {
        if (body[i] != '\\' || i + 2 >= body.size()) { result.push_back(body[i]); continue; }
        const char escaped = body[++i];
        switch (escaped) {
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case '\\': result.push_back('\\'); break;
        case '"': result.push_back('"'); break;
        default: result.push_back(escaped); break;
        }
    }
    return result;
}
}

std::optional<Snapshot> parsePrometheus(std::string_view body) {
    const std::string normalizedBody = normalizeMetricsBody(body);
    body = normalizedBody;
    Snapshot result;
    bool sawRequests = false;
    size_t start = 0;
    while (start < body.size()) {
        const size_t end = body.find('\n', start);
        const auto line = body.substr(start, end == std::string_view::npos ? body.size() - start : end - start);
        start = end == std::string_view::npos ? body.size() : end + 1;
        if (line.empty() || line.front() == '#') continue;
        const size_t separator = line.find_first_of(" \t");
        if (separator == std::string_view::npos) continue;
        auto name = line.substr(0, separator);
        if (const size_t labels = name.find('{'); labels != std::string_view::npos) name = name.substr(0, labels);
        const size_t valueStart = line.find_first_not_of(" \t", separator);
        if (valueStart == std::string_view::npos) continue;
        const auto valueText = line.substr(valueStart);
        const size_t valueEnd = valueText.find_first_of(" \t");
        const auto value = metricValue(valueText.substr(0, valueEnd));
        if (!value) continue;
        if (metricNameMatches(name, "requests_processing")) {
            result.requestsProcessing = static_cast<int>(*value);
            sawRequests = true;
        } else if (metricNameMatches(name, "predicted_tokens_seconds")) {
            result.predictedTokensPerSecond = *value;
        } else if (metricNameMatches(name, "tokens_predicted_total")) {
            result.tokensPredictedTotal = *value;
        }
    }
    return sawRequests ? std::optional<Snapshot>(std::move(result)) : std::nullopt;
}

void LlamaMetrics::reset() {
    processId_ = 0;
    previousTokensPredicted_.reset();
    previousSample_ = {};
}

std::string LlamaMetrics::sample(const std::string& host, unsigned short port, unsigned long processId) {
    if (!processId) { reset(); return "n/a"; }
    if (processId_ != processId) {
        processId_ = processId;
        previousTokensPredicted_.reset();
        previousSample_ = {};
    }
    const auto body = getMetrics(host, port);
    if (!body) return "n/a";
    const auto current = parsePrometheus(*body);
    if (!current) return "n/a";

    const auto now = std::chrono::steady_clock::now();
    std::optional<double> speed;
    if (current->requestsProcessing > 0) {
        if (!speed && current->tokensPredictedTotal && previousTokensPredicted_ && previousSample_ != std::chrono::steady_clock::time_point{}) {
            const double elapsed = std::chrono::duration<double>(now - previousSample_).count();
            const double delta = *current->tokensPredictedTotal - *previousTokensPredicted_;
            if (elapsed > 0.0 && delta > 0.0) speed = delta / elapsed;
        }
        if (!speed && current->predictedTokensPerSecond && *current->predictedTokensPerSecond > 0.0)
            speed = current->predictedTokensPerSecond;
    }
    if (current->tokensPredictedTotal) previousTokensPredicted_ = current->tokensPredictedTotal;
    previousSample_ = now;
    if (current->requestsProcessing <= 0) return "idle";
    if (!speed || !std::isfinite(*speed) || *speed <= 0.0) return "n/a";
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << *speed << " tok/s";
    return output.str();
}
}
