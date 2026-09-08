#include "ModelDashboard.h"

#include "ApiClient.h"
#include "core/AppConfig.h"

#include <atomic>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <commctrl.h>

namespace lvk::gui {
namespace {

constexpr int kListId = 3001;
constexpr int kApplyId = 3003;
constexpr int kContextId = 3004;
constexpr int kThreadsId = 3005;
constexpr int kGpuLayersId = 3006;
constexpr int kBatchId = 3007;
constexpr int kKvGpuId = 3008;
constexpr int kFlashId = 3009;
constexpr int kMmapId = 3010;
constexpr int kMlockId = 3011;
constexpr UINT_PTR kTimerId = 10;
constexpr UINT kResultMessage = WM_APP + 40;

struct State {
    HWND title{};
    HWND list{};
    HWND activity{};
    HWND settings{};
    HWND apply{};
    HWND contextLabel{};
    HWND threadsLabel{};
    HWND gpuLayersLabel{};
    HWND batchLabel{};
    HWND context{};
    HWND threads{};
    HWND gpuLayers{};
    HWND batch{};
    HWND kvGpu{};
    HWND flash{};
    HWND mmap{};
    HWND mlock{};
    std::atomic<bool> pending = false;
};

struct Result {
    std::shared_ptr<State> state;
    std::wstring text;
    bool command = false;
    bool transportOk = false;
};

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring controlText(HWND control) {
    const int size = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<size_t>(size) + 1);
    GetWindowTextW(control, buffer.data(), size + 1);
    return {buffer.data(), static_cast<size_t>(size)};
}

std::string escapeJson(std::string value) {
    std::string result;
    for (const char character : value) {
        if (character == '\\') result += "\\\\";
        else if (character == '"') result += "\\\"";
        else result += character;
    }
    return result;
}

bool responseValue(const std::string& json, std::string& value) {
    const auto marker = json.find("\"result\":\"");
    if (marker == std::string::npos) return false;
    for (std::size_t i = marker + 10; i < json.size();) {
        const char character = json[i++];
        if (character == '"') return true;
        if (character == '\\' && i < json.size()) {
            const char escaped = json[i++];
            if (escaped == 'n') value += '\n';
            else if (escaped == 'r') value += '\r';
            else if (escaped == 't') value += '\t';
            else value += escaped;
        } else value += character;
    }
    return false;
}

HWND make(HWND parent, const wchar_t* className, const wchar_t* caption, DWORD style, int id = 0) {
    return CreateWindowExW(0, className, caption, WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
}

HWND makeEx(DWORD extendedStyle, HWND parent, const wchar_t* className, const wchar_t* caption,
    DWORD style, int id) {
    return CreateWindowExW(extendedStyle, className, caption, WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
}

void setRows(HWND list, const std::wstring& value) {
    ListView_DeleteAllItems(list);
    std::wistringstream lines(value);
    std::wstring line;
    int index = 0;
    while (std::getline(lines, line)) {
        const auto colon = line.find(L':');
        const std::wstring name = colon == std::wstring::npos ? line : line.substr(0, colon);
        const std::wstring detail = colon == std::wstring::npos ? L"" : line.substr(colon + 1);
        LVITEMW row{};
        row.mask = LVIF_TEXT;
        row.iItem = index++;
        row.pszText = const_cast<wchar_t*>(name.c_str());
        const int inserted = ListView_InsertItem(list, &row);
        ListView_SetItemText(list, inserted, 1, const_cast<wchar_t*>(detail.c_str()));
    }
}

void request(HWND window, const std::shared_ptr<State>& state, const std::wstring& command) {
    bool expected = false;
    if (!state->pending.compare_exchange_strong(expected, true)) return;
    std::thread([window, state, command] {
        ApiClient api(core::kDefaultApiHost, core::kDefaultApiPort);
        const auto response = api.postJson("/api/v1/command",
            "{\"command\":\"" + escapeJson(wideToUtf8(command)) + "\"}");
        std::string output;
        if (!response.transportOk) output = "Connection failed: " + response.error;
        else if (!responseValue(response.body, output)) output = response.body;
        auto* result = new Result{state, utf8ToWide(output), command != L"model status", response.transportOk};
        state->pending = false;
        if (!PostMessageW(window, kResultMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }).detach();
}

void apply(HWND window, const std::shared_ptr<State>& state) {
    const int selection = static_cast<int>(SendMessageW(state->flash, CB_GETCURSEL, 0, 0));
    const int flash = selection == 1 ? 1 : selection == 2 ? 0 : -1;
    const auto checkbox = [](HWND control) {
        return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0";
    };
    const std::wstring command = L"model config " + controlText(state->context) + L" "
        + controlText(state->threads) + L" " + controlText(state->gpuLayers) + L" "
        + controlText(state->batch) + L" " + checkbox(state->kvGpu) + L" "
        + std::to_wstring(flash) + L" " + checkbox(state->mmap) + L" " + checkbox(state->mlock);
    SetWindowTextW(state->activity, L"Applying settings and reloading the model...");
    request(window, state, command);
}

void layout(HWND window, const std::shared_ptr<State>& state) {
    RECT client{};
    GetClientRect(window, &client);
    constexpr int margin = 14;
    constexpr int panelHeight = 174;
    const int width = client.right;
    const int height = client.bottom;
    const int panelY = height - panelHeight - margin;
    MoveWindow(state->title, margin, 12, width - margin * 2, 17, TRUE);
    MoveWindow(state->list, margin, 34, width - margin * 2, panelY - 44, TRUE);
    MoveWindow(state->settings, margin, panelY, width - margin * 2, panelHeight, TRUE);
    const int x = margin + 14;
    const int y = panelY + 28;
    MoveWindow(state->contextLabel, x, y - 17, 90, 16, TRUE);
    MoveWindow(state->threadsLabel, x + 102, y - 17, 82, 16, TRUE);
    MoveWindow(state->gpuLayersLabel, x + 196, y - 17, 88, 16, TRUE);
    MoveWindow(state->batchLabel, x + 296, y - 17, 88, 16, TRUE);
    MoveWindow(state->context, x, y, 90, 24, TRUE);
    MoveWindow(state->threads, x + 102, y, 82, 24, TRUE);
    MoveWindow(state->gpuLayers, x + 196, y, 88, 24, TRUE);
    MoveWindow(state->batch, x + 296, y, 88, 24, TRUE);
    MoveWindow(state->kvGpu, x, y + 40, 130, 22, TRUE);
    MoveWindow(state->flash, x + 142, y + 38, 124, 190, TRUE);
    MoveWindow(state->mmap, x + 278, y + 40, 132, 22, TRUE);
    MoveWindow(state->mlock, x + 420, y + 40, 138, 22, TRUE);
    MoveWindow(state->activity, x, y + 72, width - margin * 2 - 142, 38, TRUE);
    MoveWindow(state->apply, width - margin - 116, panelY + panelHeight - 40, 102, 26, TRUE);
}

LRESULT CALLBACK dashboardProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* stored = reinterpret_cast<std::shared_ptr<State>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    const std::shared_ptr<State> state = stored ? *stored : nullptr;
    switch (message) {
    case WM_CREATE: {
        auto created = std::make_shared<State>();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new std::shared_ptr<State>(created)));
        const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        created->title = make(window, L"STATIC", L"MODEL ACTIVITY AND PLACEMENT", 0);
        created->list = makeEx(WS_EX_CLIENTEDGE, window, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL, kListId);
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(L"Parameter");
        column.cx = 190;
        ListView_InsertColumn(created->list, 0, &column);
        column.pszText = const_cast<wchar_t*>(L"Current value");
        column.cx = 520;
        ListView_InsertColumn(created->list, 1, &column);
        created->settings = make(window, L"BUTTON", L"llama.cpp memory and execution settings", BS_GROUPBOX);
        created->contextLabel = make(window, L"STATIC", L"Context", 0);
        created->threadsLabel = make(window, L"STATIC", L"Threads", 0);
        created->gpuLayersLabel = make(window, L"STATIC", L"GPU layers", 0);
        created->batchLabel = make(window, L"STATIC", L"Batch", 0);
        created->context = makeEx(WS_EX_CLIENTEDGE, window, L"EDIT", L"4096", ES_AUTOHSCROLL, kContextId);
        created->threads = makeEx(WS_EX_CLIENTEDGE, window, L"EDIT", L"0", ES_AUTOHSCROLL, kThreadsId);
        created->gpuLayers = makeEx(WS_EX_CLIENTEDGE, window, L"EDIT", L"0", ES_AUTOHSCROLL, kGpuLayersId);
        created->batch = makeEx(WS_EX_CLIENTEDGE, window, L"EDIT", L"512", ES_AUTOHSCROLL, kBatchId);
        created->kvGpu = make(window, L"BUTTON", L"KV cache on GPU", BS_AUTOCHECKBOX, kKvGpuId);
        created->flash = makeEx(WS_EX_CLIENTEDGE, window, L"COMBOBOX", L"", CBS_DROPDOWNLIST, kFlashId);
        SendMessageW(created->flash, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Flash attention: Auto"));
        SendMessageW(created->flash, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Flash attention: On"));
        SendMessageW(created->flash, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Flash attention: Off"));
        SendMessageW(created->flash, CB_SETCURSEL, 0, 0);
        created->mmap = make(window, L"BUTTON", L"Memory-map model file", BS_AUTOCHECKBOX, kMmapId);
        SendMessageW(created->mmap, BM_SETCHECK, BST_CHECKED, 0);
        created->mlock = make(window, L"BUTTON", L"Lock model in RAM", BS_AUTOCHECKBOX, kMlockId);
        created->activity = make(window, L"STATIC", L"Waiting for model status...", 0);
        created->apply = make(window, L"BUTTON", L"Apply / reload", BS_PUSHBUTTON, kApplyId);
        for (const HWND control : {created->title, created->list, created->settings, created->contextLabel, created->threadsLabel,
                 created->gpuLayersLabel, created->batchLabel, created->context, created->threads,
                 created->gpuLayers, created->batch, created->kvGpu, created->flash, created->mmap,
                 created->mlock, created->activity, created->apply}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        layout(window, created);
        request(window, created, L"model status");
        SetTimer(window, kTimerId, 1000, nullptr);
        return 0;
    }
    case WM_SIZE:
        if (state) layout(window, state);
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {720, 590};
        return 0;
    case WM_TIMER:
        if (state && wParam == kTimerId) request(window, state, L"model status");
        return 0;
    case WM_COMMAND:
        if (state && LOWORD(wParam) == kApplyId && HIWORD(wParam) == BN_CLICKED) {
            apply(window, state);
            return 0;
        }
        break;
    case kResultMessage: {
        std::unique_ptr<Result> result(reinterpret_cast<Result*>(lParam));
        if (!state || result->state.get() != state.get()) return 0;
        if (result->command) SetWindowTextW(state->activity, result->text.c_str());
        else {
            setRows(state->list, result->text);
            SetWindowTextW(state->activity, result->transportOk
                ? L"Live status: refreshed every second. Settings reload the active model."
                : L"Core is unavailable. Start or restart it in the main window.");
        }
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, kTimerId);
        delete stored;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

void openModelDashboard(HINSTANCE instance, HWND owner) {
    constexpr wchar_t kClassName[] = L"AI-Agent-LVK-Model-Dashboard";
    static bool registered = false;
    if (!registered) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = dashboardProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kClassName;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        registered = RegisterClassW(&windowClass) != 0;
    }
    const HWND window = CreateWindowExW(0, kClassName, L"AI-Agent-LVK — Model Dashboard",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 850, 690, owner, nullptr, instance, nullptr);
    if (window) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }
}

} // namespace lvk::gui
