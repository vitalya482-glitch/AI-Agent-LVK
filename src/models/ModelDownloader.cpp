#include "models/ModelDownloader.h"
#include "util/Text.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <array>
#include <chrono>
#include <memory>
#include <vector>

namespace lvk::models {
namespace {
struct File {
    HANDLE h=INVALID_HANDLE_VALUE;
    ~File(){close();}
    void close(){if(h!=INVALID_HANDLE_VALUE){CloseHandle(h);h=INVALID_HANDLE_VALUE;}}
};
struct Internet {HINTERNET h{};~Internet(){if(h)WinHttpCloseHandle(h);}};
struct Cancelled {};
void checkCancel(const std::atomic<bool>& cancel){if(cancel.load())throw Cancelled{};}
[[noreturn]] void fail(const std::string& action) {throw std::runtime_error(action+": "+util::winError(GetLastError()));}
void httpCheck(BOOL ok,const std::atomic<bool>& cancel,const char* action){checkCancel(cancel);if(!ok)fail(action);}
// Native async WinHTTP keeps cancellation independent of DNS/connect/read timeouts.
// The request, callback context AND read buffer survive until HANDLE_CLOSING.
class AsyncRequest {
    HANDLE ready=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    HANDLE closed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<DWORD> error{0},received{0};
    bool callbackInstalled=false;
    static void CALLBACK callback(HINTERNET,DWORD_PTR context,DWORD status,void* data,DWORD length){
        auto* self=reinterpret_cast<AsyncRequest*>(context);if(!self)return;
        if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){SetEvent(self->closed);return;}
        if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR){self->error=static_cast<WINHTTP_ASYNC_RESULT*>(data)->dwError;SetEvent(self->ready);}
        else if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE){self->received=length;SetEvent(self->ready);}
        else if(status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE||status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)SetEvent(self->ready);
    }
public:
    HINTERNET h{};
    std::array<char,65536> buffer{};
    explicit AsyncRequest(HINTERNET request):h(request){}
    void initialize(){
        if(!h||!ready||!closed)fail("Cannot initialize asynchronous download");
        DWORD_PTR context=reinterpret_cast<DWORD_PTR>(this);
        if(!WinHttpSetOption(h,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context)))fail("Cannot set download context");
        if(WinHttpSetStatusCallback(h,callback,WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE|WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE|WINHTTP_CALLBACK_FLAG_READ_COMPLETE|WINHTTP_CALLBACK_FLAG_REQUEST_ERROR|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)
            fail("Cannot set asynchronous download callback");
        callbackInstalled=true;
    }
    ~AsyncRequest(){
        if(h){WinHttpCloseHandle(h);if(callbackInstalled)WaitForSingleObject(closed,INFINITE);}
        if(ready)CloseHandle(ready);if(closed)CloseHandle(closed);
    }
    template<class Operation> DWORD perform(Operation operation,const std::atomic<bool>& cancel,const char* action){
        checkCancel(cancel);ResetEvent(ready);error=0;received=0;
        if(!operation()){const DWORD code=GetLastError();if(code!=ERROR_IO_PENDING){SetLastError(code);fail(action);}}
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(WaitForSingleObject(ready,100)!=WAIT_OBJECT_0){
            checkCancel(cancel);
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error(std::string(action)+": network timeout.");
        }
        checkCancel(cancel);
        if(error){SetLastError(error);fail(action);}
        return received;
    }
    DWORD_PTR context()const{return reinterpret_cast<DWORD_PTR>(this);}
};
class Hash {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
public:
    Hash(){
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
            throw std::runtime_error("Cannot open SHA-256 provider.");
        if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0){
            BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;
            throw std::runtime_error("Cannot create SHA-256 hash.");
        }
    }
    ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    void add(const char* bytes,DWORD count){if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes)),count,0)<0)throw std::runtime_error("SHA-256 update failed.");}
    std::string finish(){std::array<UCHAR,32> bytes{};if(BCryptFinishHash(hash,bytes.data(),32,0)<0)throw std::runtime_error("SHA-256 failed.");std::string text;for(auto b:bytes){text+="0123456789abcdef"[b>>4];text+="0123456789abcdef"[b&15];}return text;}
};
void validateFilename(const std::string& filename){
    if(filename.empty()||filename.find_first_of("/\\:\"<>|?*")!=std::string::npos||filename.find("..")!=std::string::npos)
        throw std::runtime_error("Invalid catalog filename.");
    for(unsigned char c:filename)if(c<32)throw std::runtime_error("Invalid catalog filename.");
    if(std::filesystem::path(util::wide(filename)).extension()!=L".gguf")throw std::runtime_error("Catalog downloads must be GGUF data files.");
}
void rejectReparse(HANDLE file){
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file,&info))fail("Cannot inspect file");
    if(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Refusing to overwrite a reparse point or directory.");
    if(info.nNumberOfLinks>1)throw std::runtime_error("Refusing to overwrite a hard-linked file.");
}
void verify(const ModelCatalogEntry& entry,std::uint64_t bytes,Hash& hash,const std::array<char,4>& magic){
    if(bytes<4||magic!=std::array<char,4>{'G','G','U','F'})throw std::runtime_error("Downloaded file is not GGUF data.");
    if(entry.expectedSize&&entry.expectedSize!=bytes)throw std::runtime_error("Size mismatch: expected "+std::to_string(entry.expectedSize)+", received "+std::to_string(bytes)+" bytes.");
    const auto actual=hash.finish();
    if(!entry.sha256.empty()&&_stricmp(actual.c_str(),entry.sha256.c_str())!=0)throw std::runtime_error("SHA-256 hash mismatch. The .part file is not registered.");
}
}
DownloadResult downloadModel(const DownloadRequest& request,const std::atomic<bool>& cancel,const std::function<void(const DownloadProgress&)>& progress){
    DownloadResult result;
    try{
        const auto started=std::chrono::steady_clock::now();auto last=started-std::chrono::seconds(1);
        auto publish=[&](std::uint64_t bytes,std::uint64_t total,const char* phase,bool force=false){
            const auto now=std::chrono::steady_clock::now();if(!force&&now-last<std::chrono::milliseconds(150))return;
            const double seconds=std::chrono::duration<double>(now-started).count();last=now;
            progress({bytes,total,seconds,seconds>0?static_cast<double>(bytes)/seconds:0,phase});
        };
        checkCancel(cancel);validateFilename(request.entry.filename);publish(0,0,"Checking destination",true);
        if(request.directory.empty()||!request.directory.is_absolute()||!std::filesystem::is_directory(request.directory))
            throw std::runtime_error("Choose an existing absolute install folder.");
        result.path=request.directory/util::wide(request.entry.filename);
        auto part=result.path;part+=L".part";
        const bool finalExists=std::filesystem::exists(result.path);
        if(finalExists&&request.existing==ExistingPolicy::Ask){result.status=DownloadStatus::NeedExistingChoice;return result;}
        if(finalExists&&request.existing==ExistingPolicy::UseExisting){
            File file;file.h=CreateFileW(result.path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
            if(file.h==INVALID_HANDLE_VALUE)fail("Cannot read existing GGUF");
            Hash hash;std::array<char,65536> buffer{};std::array<char,4> magic{};std::uint64_t bytes=0;
            for(;;){checkCancel(cancel);DWORD read{};if(!ReadFile(file.h,buffer.data(),static_cast<DWORD>(buffer.size()),&read,nullptr))fail("Cannot read existing GGUF");if(!read)break;
                for(DWORD i=0;i<read&&bytes+i<4;++i)magic[static_cast<size_t>(bytes+i)]=buffer[i];
                hash.add(buffer.data(),read);bytes+=read;publish(bytes,request.entry.expectedSize,"Verifying existing GGUF");}
            verify(request.entry,bytes,hash,magic);checkCancel(cancel);
            result.status=DownloadStatus::Complete;result.message="Existing GGUF verified.";return result;
        }
        if(std::filesystem::exists(part)&&!request.restartPartial){result.status=DownloadStatus::NeedPartialChoice;return result;}
        // A unique delete-on-close probe never overwrites the user's data.
        const auto probePath=request.directory/util::wide(".lvk-write-"+newModelId()+".tmp");
        {File probe;probe.h=CreateFileW(probePath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
         if(probe.h==INVALID_HANDLE_VALUE)fail("Install folder is not writable");}
        ULARGE_INTEGER available{};
        if(!GetDiskFreeSpaceExW(request.directory.c_str(),&available,nullptr,nullptr))fail("Cannot check free disk space");
        const auto reserve=std::max<std::uint64_t>(512ULL*1024*1024,request.entry.expectedSize/20);
        if(request.entry.expectedSize>available.QuadPart||reserve>available.QuadPart-request.entry.expectedSize)
            throw std::runtime_error("Insufficient free disk space (model size plus 5% / 512 MiB reserve required).");
        const auto url=util::wide(request.entry.downloadUrl);
        URL_COMPONENTS parts{sizeof(parts)};parts.dwSchemeLength=parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=static_cast<DWORD>(-1);
        if(!WinHttpCrackUrl(url.c_str(),0,0,&parts))fail("Invalid download URL");
        const std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
        // HTTP is accepted ONLY on numeric loopback, for deterministic transport tests.
        if(parts.nScheme!=INTERNET_SCHEME_HTTPS&&!(parts.nScheme==INTERNET_SCHEME_HTTP&&host==L"127.0.0.1"))
            throw std::runtime_error("Download URL must use HTTPS.");
        Internet session{WinHttpOpen(L"AI-Agent-LVK Model Downloader",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC)};
        if(!session.h)fail("Cannot initialize WinHTTP");
        WinHttpSetTimeouts(session.h,10000,10000,30000,30000);
        Internet connection{WinHttpConnect(session.h,host.c_str(),parts.nPort,0)};
        if(!connection.h)fail("Cannot connect to download host");
        std::wstring resource(parts.lpszUrlPath,parts.dwUrlPathLength);
        if(parts.dwExtraInfoLength)resource.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
        AsyncRequest http(WinHttpOpenRequest(connection.h,L"GET",resource.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0));
        http.initialize();
        DWORD redirects=10,policy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(http.h,WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,&redirects,sizeof(redirects));
        WinHttpSetOption(http.h,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
        checkCancel(cancel);publish(0,0,"Connecting",true);
        http.perform([&]{return WinHttpSendRequest(http.h,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,http.context());},cancel,"Network/TLS request failed");
        http.perform([&]{return WinHttpReceiveResponse(http.h,nullptr);},cancel,"Network/TLS/redirect response failed");
        DWORD status{},length=sizeof(status);
        httpCheck(WinHttpQueryHeaders(http.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX),cancel,"Cannot read HTTP status");
        if(status!=200)throw std::runtime_error("HTTP "+std::to_string(status)+" from download server.");
        wchar_t sizeHeader[64]{};length=sizeof(sizeHeader);std::uint64_t total=0;
        if(WinHttpQueryHeaders(http.h,WINHTTP_QUERY_CONTENT_LENGTH,WINHTTP_HEADER_NAME_BY_INDEX,sizeHeader,&length,WINHTTP_NO_HEADER_INDEX))total=std::stoull(sizeHeader);
        if(total&&request.entry.expectedSize&&total!=request.entry.expectedSize)throw std::runtime_error("HTTP Content-Length does not match catalog size.");
        File file;
        file.h=CreateFileW(part.c_str(),GENERIC_WRITE,0,nullptr,request.restartPartial?OPEN_ALWAYS:CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(file.h==INVALID_HANDLE_VALUE)fail("Cannot create .part file");
        rejectReparse(file.h);if(!SetEndOfFile(file.h))fail("Cannot restart .part file");
        Hash hash;auto& buffer=http.buffer;std::array<char,4> magic{};std::uint64_t bytes=0;
        for(;;){
            const DWORD read=http.perform([&]{return WinHttpReadData(http.h,buffer.data(),static_cast<DWORD>(buffer.size()),nullptr);},cancel,"Network read failed / timeout");
            if(!read)break;
            for(DWORD i=0;i<read&&bytes+i<4;++i)magic[static_cast<size_t>(bytes+i)]=buffer[i];
            DWORD written{};if(!WriteFile(file.h,buffer.data(),read,&written,nullptr)||written!=read)fail("Cannot write .part (disk full or access denied)");
            hash.add(buffer.data(),read);bytes+=read;
            if(request.entry.expectedSize&&bytes>request.entry.expectedSize)throw std::runtime_error("Download exceeds expected size.");
            publish(bytes,total,"Downloading");
        }
        if(total&&bytes!=total)throw std::runtime_error("Truncated HTTP response.");
        verify(request.entry,bytes,hash,magic);checkCancel(cancel);
        if(!FlushFileBuffers(file.h))fail("Cannot flush GGUF file");file.close();checkCancel(cancel);
        // No replacement unless the user explicitly chose Re-download. Rename occurs
        // within the selected directory / volume, after every validation has passed.
        if(!MoveFileExW(part.c_str(),result.path.c_str(),MOVEFILE_WRITE_THROUGH|(request.existing==ExistingPolicy::Replace?MOVEFILE_REPLACE_EXISTING:0)))fail("Cannot rename .part to GGUF");
        publish(bytes,total,"Verified and installed",true);result.status=DownloadStatus::Complete;result.message="GGUF downloaded and verified.";
    }catch(const Cancelled&){result.status=DownloadStatus::Cancelled;result.message="Download cancelled. Any .part is retained, not registered.";}
    catch(const std::exception& error){result.status=DownloadStatus::Failed;result.message=error.what();}
    return result;
}
}
