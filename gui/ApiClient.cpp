#include "ApiClient.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <limits>
#include <string>
#include <vector>

namespace lvk::gui {
namespace {

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::string winHttpError(const char* operation) {
    return std::string(operation) + " failed. WinHTTP error: " + std::to_string(GetLastError());
}

} // namespace

ApiClient::ApiClient(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port) {
}

ApiResponse ApiClient::get(const std::string& path) const {
    return request(L"GET", path, {});
}

ApiResponse ApiClient::postJson(const std::string& path, const std::string& body) const {
    return request(L"POST", path, body);
}

ApiResponse ApiClient::request(
    const wchar_t* method,
    const std::string& path,
    const std::string& body) const {

    ApiResponse result;

    const HINTERNET session = WinHttpOpen(
        L"AI-Agent-LVK-GUI/0.1",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        result.error = winHttpError("WinHttpOpen");
        return result;
    }

    WinHttpSetTimeouts(session, 1000, 1000, 2000, 5000);

    const std::wstring wideHost = utf8ToWide(host_);
    const HINTERNET connection = WinHttpConnect(
        session,
        wideHost.c_str(),
        static_cast<INTERNET_PORT>(port_),
        0);
    if (!connection) {
        result.error = winHttpError("WinHttpConnect");
        WinHttpCloseHandle(session);
        return result;
    }

    const std::wstring widePath = utf8ToWide(path);
    const HINTERNET requestHandle = WinHttpOpenRequest(
        connection,
        method,
        widePath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0);
    if (!requestHandle) {
        result.error = winHttpError("WinHttpOpenRequest");
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    const wchar_t* headers = body.empty()
        ? L"Accept: application/json\r\n"
        : L"Content-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";

    if (body.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
        result.error = "Request body is too large.";
        WinHttpCloseHandle(requestHandle);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    const DWORD bodyLength = static_cast<DWORD>(body.size());
    LPVOID bodyData = body.empty()
        ? WINHTTP_NO_REQUEST_DATA
        : const_cast<char*>(body.data());

    const BOOL sent = WinHttpSendRequest(
        requestHandle,
        headers,
        static_cast<DWORD>(-1L),
        bodyData,
        bodyLength,
        bodyLength,
        0);

    if (!sent || !WinHttpReceiveResponse(requestHandle, nullptr)) {
        result.error = winHttpError("HTTP request");
        WinHttpCloseHandle(requestHandle);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (WinHttpQueryHeaders(
            requestHandle,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
        result.statusCode = static_cast<int>(statusCode);
    }

    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(requestHandle, &available)) {
            result.error = winHttpError("WinHttpQueryDataAvailable");
            break;
        }

        if (available == 0) {
            result.transportOk = true;
            break;
        }

        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(requestHandle, buffer.data(), available, &read)) {
            result.error = winHttpError("WinHttpReadData");
            break;
        }

        result.body.append(buffer.data(), static_cast<std::size_t>(read));
    }

    WinHttpCloseHandle(requestHandle);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

} // namespace lvk::gui
