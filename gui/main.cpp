#include <windows.h>
#include <commdlg.h>
#include <urlmon.h>

#include "ApiClient.h"
#include "ChatWindow.h"
#include "core/AppConfig.h"

#include <cctype>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef AI_AGENT_LVK_VERSION
#define AI_AGENT_LVK_VERSION "0.0.0-dev"
#endif

namespace {
constexpr int kStatusId = 1001, kHistoryId = 1002, kInputId = 1003, kSendId = 1004;
constexpr int kStartCoreId = 1005, kRestartCoreId = 1006, kUpdateId = 1007, kModelStatusId = 1008;
constexpr int kBrowseModelId = 1009, kModelUrlId = 1010, kDownloadModelId = 1011, kContextId = 1012;
constexpr int kThreadsId = 1013, kGpuLayersId = 1014, kApplyConfigId = 1015, kCoreFrameId = 1016;
constexpr int kModelFrameId = 1017, kChatFrameId = 1018, kOpenChatId = 1019;
constexpr int kDownloadStatusId = 1020, kTelemetryId = 1021;
constexpr UINT_PTR kStatusTimerId = 1;
constexpr UINT kStatusPollMs = 2000, kDownloadComplete = WM_APP + 1, kDownloadProgress = WM_APP + 2, kAsyncStatus = WM_APP + 3, kAsyncCommand = WM_APP + 4;
constexpr wchar_t kCoreBridgeClass[] = L"AI_AGENT_LVK_UPDATE_BRIDGE";
HWND gStatus{}, gHistory{}, gInput{}, gSend{}, gStartCore{}, gRestartCore{}, gUpdate{}, gModelStatus{}, gBrowseModel{}, gModelUrl{}, gDownloadModel{}, gDownloadStatus{}, gTelemetry{}, gOpenChat{}, gContext{}, gThreads{}, gGpuLayers{}, gApplyConfig{}, gCoreFrame{}, gModelFrame{}, gChatFrame{};
bool gConnected = false, gRestartPending = false, gDownloading = false;
std::atomic<bool> gStatusRequestActive = false;
HINSTANCE gInstance = nullptr;
lvk::gui::ApiClient gApi(lvk::core::kDefaultApiHost, lvk::core::kDefaultApiPort);

std::wstring utf8ToWide(const std::string& v) { if (v.empty()) return {}; const int n = MultiByteToWideChar(CP_UTF8, 0, v.data(), static_cast<int>(v.size()), nullptr, 0); if (n <= 0) return L"[invalid UTF-8]"; std::wstring r(static_cast<size_t>(n), L'\0'); MultiByteToWideChar(CP_UTF8, 0, v.data(), static_cast<int>(v.size()), r.data(), n); return r; }
std::string wideToUtf8(const std::wstring& v) { if (v.empty()) return {}; const int n = WideCharToMultiByte(CP_UTF8, 0, v.data(), static_cast<int>(v.size()), nullptr, 0, nullptr, nullptr); if (n <= 0) return {}; std::string r(static_cast<size_t>(n), '\0'); WideCharToMultiByte(CP_UTF8, 0, v.data(), static_cast<int>(v.size()), r.data(), n, nullptr, nullptr); return r; }
std::wstring getText(HWND w) { const int n = GetWindowTextLengthW(w); if (n <= 0) return {}; std::vector<wchar_t> b(static_cast<size_t>(n) + 1); GetWindowTextW(w, b.data(), n + 1); return {b.data(), static_cast<size_t>(n)}; }
void appendHistory(const std::wstring& text) { const LRESULT n = SendMessageW(gHistory, WM_GETTEXTLENGTH, 0, 0); SendMessageW(gHistory, EM_SETSEL, n, n); SendMessageW(gHistory, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str())); }
std::string jsonEscape(std::string_view v) { std::string r; for (char c : v) { switch (c) { case '\\': r += "\\\\"; break; case '"': r += "\\\""; break; case '\n': r += "\\n"; break; case '\r': r += "\\r"; break; case '\t': r += "\\t"; break; default: r += c; } } return r; }
bool extractJsonString(const std::string& json, std::string_view field, std::string& value) { const auto f = json.find("\"" + std::string(field) + "\""); if (f == std::string::npos) return false; auto p = json.find(':', f + field.size() + 2); if (p == std::string::npos) return false; while (++p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) {} if (p >= json.size() || json[p++] != '"') return false; std::string r; while (p < json.size()) { const char c = json[p++]; if (c == '"') { value = std::move(r); return true; } if (c != '\\') { r += c; continue; } if (p >= json.size()) return false; switch (json[p++]) { case '"': r += '"'; break; case '\\': r += '\\'; break; case 'n': r += '\n'; break; case 'r': r += '\r'; break; case 't': r += '\t'; break; default: return false; } } return false; }
std::filesystem::path executableDirectory() { std::vector<wchar_t> b(32768); const DWORD n = GetModuleFileNameW(nullptr, b.data(), static_cast<DWORD>(b.size())); return n == 0 || n >= b.size() ? std::filesystem::current_path() : std::filesystem::path(std::wstring(b.data(), n)).parent_path(); }

void setConnected(bool connected) { gConnected = connected; SetWindowTextW(gStatus, connected ? L"● Core connected  •  API 127.0.0.1:7842" : L"○ Core disconnected"); EnableWindow(gStartCore, connected ? FALSE : TRUE); EnableWindow(gRestartCore, TRUE); EnableWindow(gUpdate, connected ? TRUE : FALSE); EnableWindow(gModelStatus, connected ? TRUE : FALSE); EnableWindow(gBrowseModel, connected ? TRUE : FALSE); EnableWindow(gApplyConfig, connected ? TRUE : FALSE); }
bool startCore() { if (gConnected) { appendHistory(L"[core] Already running.\r\n\r\n"); return true; } const auto dir = executableDirectory(), core = dir / L"AI-Agent-LVK.exe"; std::error_code ec; if (!std::filesystem::exists(core, ec)) { appendHistory(L"[core] AI-Agent-LVK.exe was not found next to the GUI.\r\n\r\n"); return false; } std::wstring line = L"\"" + core.wstring() + L"\" --headless"; std::vector<wchar_t> mutableLine(line.begin(), line.end()); mutableLine.push_back(L'\0'); STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; PROCESS_INFORMATION pi{}; if (!CreateProcessW(core.c_str(), mutableLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) { appendHistory(L"[core] Start failed; Win32 error: " + std::to_wstring(GetLastError()) + L"\r\n\r\n"); return false; } CloseHandle(pi.hThread); CloseHandle(pi.hProcess); appendHistory(L"[core] Started in background. Waiting for API...\r\n\r\n"); return true; }
void restartCore() { if (!gConnected) { startCore(); return; } const HWND bridge = FindWindowW(kCoreBridgeClass, nullptr); if (!bridge || !PostMessageW(bridge, WM_CLOSE, 0, 0)) { appendHistory(L"[core] Could not request core shutdown.\r\n\r\n"); return; } gRestartPending = true; appendHistory(L"[core] Restart requested.\r\n\r\n"); }
struct StatusResult { bool connected = false; };
struct CommandResult { bool connected = false; bool telemetry = false; std::wstring output; };
void refreshStatus(HWND window) { bool expected = false; if (!gStatusRequestActive.compare_exchange_strong(expected, true)) return; std::thread([window] { const auto response = gApi.get("/api/v1/status"); auto* result = new StatusResult{response.transportOk && response.statusCode >= 200 && response.statusCode < 300}; gStatusRequestActive = false; if (!PostMessageW(window, kAsyncStatus, 0, reinterpret_cast<LPARAM>(result))) delete result; }).detach(); }
void queueCoreCommand(HWND window, const std::wstring& input, bool telemetry = false) { if (input.empty()) return; appendHistory(L"> " + input + L"\r\n"); std::thread([window, input, telemetry] { const auto response = gApi.postJson("/api/v1/command", "{\"command\":\"" + jsonEscape(wideToUtf8(input)) + "\"}"); auto* result = new CommandResult; result->connected = response.transportOk; result->telemetry = telemetry; if (!response.transportOk) result->output = L"[transport error] " + utf8ToWide(response.error); else { std::string text; if (!extractJsonString(response.body, "result", text) && !extractJsonString(response.body, "error", text)) text = response.body; result->output = utf8ToWide(text); } if (!PostMessageW(window, kAsyncCommand, 0, reinterpret_cast<LPARAM>(result))) delete result; }).detach(); }
void sendCommand(HWND window) { const auto input = getText(gInput); if (!input.empty()) { SetWindowTextW(gInput, L""); queueCoreCommand(window, input); } }
void selectModel(HWND window) { std::vector<wchar_t> f(32768); OPENFILENAMEW d{}; d.lStructSize = sizeof(d); d.hwndOwner = window; d.lpstrFilter = L"GGUF models (*.gguf)\0*.gguf\0All files\0*.*\0"; d.lpstrFile = f.data(); d.nMaxFile = static_cast<DWORD>(f.size()); d.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST; if (GetOpenFileNameW(&d)) queueCoreCommand(window, L"model load \"" + std::wstring(f.data()) + L"\""); }
std::wstring downloadFileName(const std::wstring& url) { const auto q = url.find_first_of(L"?#"); const auto path = url.substr(0, q); const auto slash = path.find_last_of(L"/"); if (slash == std::wstring::npos || slash + 1 >= path.size()) return {}; const auto name = path.substr(slash + 1); return name.find_first_of(L"\\:") != std::wstring::npos || name.size() > 180 ? L"" : name; }
class DownloadProgress final : public IBindStatusCallback {
public:
    explicit DownloadProgress(HWND window) : window_(window) {}
    STDMETHOD(QueryInterface)(REFIID iid, void** object) override { if (iid == IID_IUnknown || iid == IID_IBindStatusCallback) { *object = this; AddRef(); return S_OK; } *object = nullptr; return E_NOINTERFACE; }
    STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
    STDMETHOD_(ULONG, Release)() override { const ULONG value = --refs_; if (value == 0) delete this; return value; }
    STDMETHOD(OnStartBinding)(DWORD, IBinding*) override { return S_OK; }
    STDMETHOD(GetPriority)(LONG*) override { return E_NOTIMPL; }
    STDMETHOD(OnLowResource)(DWORD) override { return S_OK; }
    STDMETHOD(OnProgress)(ULONG current, ULONG maximum, ULONG, LPCWSTR) override { if (maximum > 0) PostMessageW(window_, kDownloadProgress, static_cast<WPARAM>((current * 100ULL) / maximum), 0); return S_OK; }
    STDMETHOD(OnStopBinding)(HRESULT, LPCWSTR) override { return S_OK; }
    STDMETHOD(GetBindInfo)(DWORD* flags, BINDINFO* info) override { if (!flags || !info) return E_POINTER; *flags = 0; info->cbSize = sizeof(*info); return S_OK; }
    STDMETHOD(OnDataAvailable)(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    STDMETHOD(OnObjectAvailable)(REFIID, IUnknown*) override { return E_NOTIMPL; }
private:
    ULONG refs_ = 1;
    HWND window_ = nullptr;
};
void downloadModel(HWND window) { if (gDownloading) return; const auto url = getText(gModelUrl), name = downloadFileName(url); if (url.rfind(L"https://", 0) != 0 || name.empty() || !std::wstring_view(name).ends_with(L".gguf")) { appendHistory(L"[download] Use a direct HTTPS link ending in .gguf.\r\n\r\n"); return; } const auto destination = executableDirectory() / L"models" / name; std::error_code ec; std::filesystem::create_directories(destination.parent_path(), ec); if (ec) { appendHistory(L"[download] Could not create the models folder.\r\n\r\n"); return; } gDownloading = true; EnableWindow(gDownloadModel, FALSE); SetWindowTextW(gDownloadModel, L"Downloading..."); SetWindowTextW(gDownloadStatus, L"Download: 0%"); appendHistory(L"[download] Starting: " + url + L"\r\n"); std::thread([window, url, destination] { auto* progress = new DownloadProgress(window); const HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), destination.c_str(), 0, progress); progress->Release(); auto* message = new std::wstring(SUCCEEDED(hr) ? L"[download] Complete: " + destination.wstring() + L"\r\nChoose GGUF to load it.\r\n\r\n" : L"[download] Failed. HRESULT: " + std::to_wstring(static_cast<long>(hr)) + L"\r\n\r\n"); PostMessageW(window, kDownloadComplete, SUCCEEDED(hr), reinterpret_cast<LPARAM>(message)); }).detach(); }
void applyConfig(HWND window) { queueCoreCommand(window, L"model config " + getText(gContext) + L" " + getText(gThreads) + L" " + getText(gGpuLayers)); }
HWND control(HWND parent, const wchar_t* klass, const wchar_t* text, DWORD style, int id) { return CreateWindowExW(0, klass, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr); }
void layoutControls(HWND w) { RECT r{}; GetClientRect(w, &r); const int width = r.right, height = r.bottom, m = 14; MoveWindow(gStatus,m,m,width-m*2,26,TRUE); MoveWindow(gCoreFrame,m,50,width-m*2,56,TRUE); MoveWindow(gStartCore,m+12,72,105,25,TRUE); MoveWindow(gRestartCore,m+125,72,110,25,TRUE); MoveWindow(gUpdate,m+243,72,90,25,TRUE); MoveWindow(gOpenChat,m+341,72,100,25,TRUE); MoveWindow(gModelFrame,m,116,width-m*2,236,TRUE); MoveWindow(gModelStatus,m+12,138,105,25,TRUE); MoveWindow(gBrowseModel,m+125,138,105,25,TRUE); MoveWindow(gContext,m+12,174,110,24,TRUE); MoveWindow(gThreads,m+130,174,95,24,TRUE); MoveWindow(gGpuLayers,m+233,174,95,24,TRUE); MoveWindow(gApplyConfig,m+336,174,110,24,TRUE); MoveWindow(gModelUrl,m+12,210,width-m*2-142,24,TRUE); MoveWindow(gDownloadModel,width-m-120,210,108,24,TRUE); MoveWindow(gDownloadStatus,m+12,240,width-m*2-24,20,TRUE); MoveWindow(gTelemetry,m+12,264,width-m*2-24,76,TRUE); const int chatY=362; MoveWindow(gChatFrame,m,chatY,width-m*2,height-chatY-m,TRUE); MoveWindow(gHistory,m+12,chatY+24,width-m*2-24,height-chatY-100,TRUE); MoveWindow(gInput,m+12,height-52,width-m*2-102,26,TRUE); MoveWindow(gSend,width-m-82,height-52,70,26,TRUE); }
LRESULT CALLBACK windowProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        gStatus = control(w, L"STATIC", L"● Checking Core...", 0, kStatusId);
        gCoreFrame = control(w, L"BUTTON", L" Core runtime ", BS_GROUPBOX, kCoreFrameId);
        gModelFrame = control(w, L"BUTTON", L" Model runtime ", BS_GROUPBOX, kModelFrameId);
        gChatFrame = control(w, L"BUTTON", L" Command log ", BS_GROUPBOX, kChatFrameId);
        gStartCore = control(w, L"BUTTON", L"Start Core", BS_PUSHBUTTON, kStartCoreId);
        gRestartCore = control(w, L"BUTTON", L"Restart Core", BS_PUSHBUTTON, kRestartCoreId);
        gUpdate = control(w, L"BUTTON", L"Update", BS_PUSHBUTTON, kUpdateId);
        gOpenChat = control(w, L"BUTTON", L"Open Chat", BS_PUSHBUTTON, kOpenChatId);
        gModelStatus = control(w, L"BUTTON", L"Refresh status", BS_PUSHBUTTON, kModelStatusId);
        gBrowseModel = control(w, L"BUTTON", L"Choose GGUF", BS_PUSHBUTTON, kBrowseModelId);
        gContext = control(w, L"EDIT", L"4096", WS_BORDER | ES_AUTOHSCROLL, kContextId);
        gThreads = control(w, L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, kThreadsId);
        gGpuLayers = control(w, L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, kGpuLayersId);
        gApplyConfig = control(w, L"BUTTON", L"Apply config", BS_PUSHBUTTON, kApplyConfigId);
        gModelUrl = control(w, L"EDIT", L"https://.../model.gguf", WS_BORDER | ES_AUTOHSCROLL, kModelUrlId);
        gDownloadModel = control(w, L"BUTTON", L"Download GGUF", BS_PUSHBUTTON, kDownloadModelId);
        gDownloadStatus = control(w, L"STATIC", L"Download: idle", 0, kDownloadStatusId);
        gTelemetry = control(w, L"STATIC", L"Model: not loaded", 0, kTelemetryId);
        gHistory = control(w, L"EDIT", L"", WS_EX_CLIENTEDGE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, kHistoryId);
        gInput = control(w, L"EDIT", L"", WS_EX_CLIENTEDGE | ES_AUTOHSCROLL, kInputId);
        gSend = control(w, L"BUTTON", L"Send", BS_PUSHBUTTON, kSendId);
        const HWND controls[] = {gStatus,gCoreFrame,gModelFrame,gChatFrame,gStartCore,gRestartCore,gUpdate,gOpenChat,gModelStatus,gBrowseModel,gContext,gThreads,gGpuLayers,gApplyConfig,gModelUrl,gDownloadModel,gDownloadStatus,gTelemetry,gHistory,gInput,gSend};
        for (const HWND item : controls) SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        appendHistory(L"AI-Agent-LVK v" + utf8ToWide(AI_AGENT_LVK_VERSION) + L"\r\nUse direct HTTPS .gguf URLs (GitHub Releases or Hugging Face resolve links).\r\n\r\n");
        refreshStatus(w); SetTimer(w, kStatusTimerId, kStatusPollMs, nullptr); SetFocus(gInput); return 0;
    }
    case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {800,800}; return 0;
    case WM_SIZE: layoutControls(w); return 0;
    case WM_TIMER: if (wp == kStatusTimerId) { refreshStatus(w); return 0; } break;
    case kAsyncStatus: { std::unique_ptr<StatusResult> result(reinterpret_cast<StatusResult*>(lp)); setConnected(result->connected); if (!result->connected && gRestartPending) { gRestartPending = false; startCore(); } return 0; }
    case kAsyncCommand: { std::unique_ptr<CommandResult> result(reinterpret_cast<CommandResult*>(lp)); setConnected(result->connected); appendHistory(result->output + L"\r\n\r\n"); if (result->telemetry) SetWindowTextW(gTelemetry, result->output.c_str()); return 0; }
    case kDownloadProgress: SetWindowTextW(gDownloadStatus, (L"Download: " + std::to_wstring(wp) + L"%").c_str()); return 0;
    case kDownloadComplete: { std::unique_ptr<std::wstring> out(reinterpret_cast<std::wstring*>(lp)); gDownloading = false; EnableWindow(gDownloadModel, TRUE); SetWindowTextW(gDownloadModel, L"Download GGUF"); SetWindowTextW(gDownloadStatus, wp ? L"Download: complete" : L"Download: failed"); appendHistory(*out); return 0; }
    case WM_COMMAND:
        if (HIWORD(wp) != BN_CLICKED) break;
        switch (LOWORD(wp)) {
        case kSendId: sendCommand(w); break;
        case kUpdateId: queueCoreCommand(w, L"update"); break;
        case kStartCoreId: startCore(); break;
        case kRestartCoreId: restartCore(); break;
        case kOpenChatId: lvk::gui::openChatWindow(gInstance, w); break;
        case kModelStatusId: queueCoreCommand(w, L"model status", true); break;
        case kBrowseModelId: selectModel(w); break;
        case kDownloadModelId: downloadModel(w); break;
        case kApplyConfigId: applyConfig(w); break;
        default: break;
        }
        SetFocus(gInput); return 0;
    case WM_DESTROY: KillTimer(w, kStatusTimerId); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) { gInstance = instance; const wchar_t name[]=L"AI-Agent-LVK-GUI-Window"; WNDCLASSW c{}; c.lpfnWndProc=windowProc;c.hInstance=instance;c.lpszClassName=name;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);if(!RegisterClassW(&c))return 1;const HWND w=CreateWindowExW(0,name,L"AI-Agent-LVK",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,900,860,nullptr,nullptr,instance,nullptr);if(!w)return 1;ShowWindow(w,show);UpdateWindow(w);MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){if(m.hwnd==gInput&&m.message==WM_KEYDOWN&&m.wParam==VK_RETURN){sendCommand(w);continue;}TranslateMessage(&m);DispatchMessageW(&m);}return static_cast<int>(m.wParam);}
