#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lvk::util {
inline std::wstring wide(std::string_view text) {
    if(text.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(!n) throw std::runtime_error("Invalid UTF-8 text.");
    std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),n);
    return out;
}
inline std::string utf8(std::wstring_view text) {
    if(text.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(!n) throw std::runtime_error("Invalid Unicode text.");
    std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),n,nullptr,nullptr);
    return out;
}
inline std::string pathText(const std::filesystem::path& path) { return utf8(path.wstring()); }
inline std::wstring quote(std::wstring_view value) {
    std::wstring result=L"\"";
    size_t slashes=0;
    for(wchar_t c:value) {
        if(c==L'\\') {++slashes;continue;}
        result.append(c==L'"'?slashes*2+1:slashes,L'\\');
        slashes=0;result+=c;
    }
    result.append(slashes*2,L'\\');result+=L'"';return result;
}
inline std::string winError(DWORD code) {
    wchar_t* buffer=nullptr;
    const DWORD n=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,code,0,reinterpret_cast<wchar_t*>(&buffer),0,nullptr);
    std::string result=n?utf8(std::wstring_view(buffer,n)):"Windows operation failed";
    if(buffer) LocalFree(buffer);
    return result+" ("+std::to_string(code)+")";
}
}
