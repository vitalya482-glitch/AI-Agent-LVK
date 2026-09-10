#include "ui/ModelSettingsWindow.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace lvk::ui {
namespace {
constexpr wchar_t kClassName[] = L"AI-Agent-LVK-ModelSettings";
constexpr int kSave = 9001, kCancel = 9002;

enum Field : size_t {
    Context, Temperature, TopK, TopP, PresencePenalty, RepeatPenalty,
    FrequencyPenalty, BatchSize, UBatchSize, Parallel, GpuLayers, CpuMoe,
    KvK, KvV, FlashAttention, SpecType, SpecDraftNMax, FieldCount
};

struct State {
    config::Profile* profile{};
    std::array<HWND, FieldCount> controls{};
    bool saved = false;
};

std::wstring wide(const std::string& s) { return {s.begin(), s.end()}; }
std::string narrow(const std::wstring& s) { return {s.begin(), s.end()}; }

std::wstring text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    if (length) GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

std::wstring number(double value) {
    std::wostringstream out;
    out << std::setprecision(8) << value;
    return out.str();
}

HWND addStatic(HWND parent, int x, int y, int w, int h, const wchar_t* value) {
    return CreateWindowW(L"STATIC", value, WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, parent, nullptr, nullptr, nullptr);
}

HWND addEdit(HWND parent, int x, int y, int w, const std::wstring& value) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, w, 24,
        parent, nullptr, nullptr, nullptr);
}

HWND addCombo(HWND parent, int x, int y, int w, const wchar_t* const* values,
              size_t count, const std::wstring& selected) {
    HWND combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        x, y, w, 200, parent, nullptr, nullptr, nullptr);
    for (size_t i = 0; i < count; ++i) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(values[i]));
    SendMessageW(combo, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(selected.c_str()));
    if (SendMessageW(combo, CB_GETCURSEL, 0, 0) == CB_ERR) SendMessageW(combo, CB_SETCURSEL, 0, 0);
    return combo;
}

void addRow(HWND window, State& s, Field field, int y, const wchar_t* label,
            const std::wstring& value, const wchar_t* help) {
    addStatic(window, 20, y + 4, 170, 20, label);
    s.controls[field] = addEdit(window, 195, y, 120, value);
    addStatic(window, 330, y + 2, 420, 34, help);
}

bool parseInt(HWND control, int minValue, int maxValue, int& result) {
    const auto value = text(control);
    if (value.empty()) return false;
    wchar_t* end{};
    const long parsed = std::wcstol(value.c_str(), &end, 10);
    if (!end || *end != L'\0' || parsed < minValue || parsed > maxValue) return false;
    result = static_cast<int>(parsed);
    return true;
}

bool parseDouble(HWND control, double minValue, double maxValue, double& result) {
    const auto value = text(control);
    if (value.empty()) return false;
    wchar_t* end{};
    const double parsed = std::wcstod(value.c_str(), &end);
    if (!end || *end != L'\0' || parsed < minValue || parsed > maxValue) return false;
    result = parsed;
    return true;
}

std::string comboValue(HWND combo) {
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index == CB_ERR) return {};
    wchar_t buffer[64]{};
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(buffer));
    return narrow(buffer);
}

bool saveValues(HWND window, State& s) {
    auto updated = *s.profile;
    bool ok = true;
    ok &= parseInt(s.controls[Context], 512, 1048576, updated.context);
    ok &= parseDouble(s.controls[Temperature], 0.0, 2.0, updated.temperature);
    ok &= parseInt(s.controls[TopK], 0, 100000, updated.topK);
    ok &= parseDouble(s.controls[TopP], 0.0, 1.0, updated.topP);
    ok &= parseDouble(s.controls[PresencePenalty], -2.0, 2.0, updated.presencePenalty);
    ok &= parseDouble(s.controls[RepeatPenalty], 0.0, 10.0, updated.repeatPenalty);
    ok &= parseDouble(s.controls[FrequencyPenalty], -2.0, 2.0, updated.frequencyPenalty);
    ok &= parseInt(s.controls[BatchSize], 1, 65536, updated.batchSize);
    ok &= parseInt(s.controls[UBatchSize], 1, 65536, updated.ubatchSize);
    ok &= parseInt(s.controls[Parallel], 1, 128, updated.parallel);
    ok &= parseInt(s.controls[GpuLayers], 0, 100000, updated.gpuLayers);
    ok &= parseInt(s.controls[CpuMoe], 0, 100000, updated.cpuMoe);
    updated.kvK = comboValue(s.controls[KvK]);
    updated.kvV = comboValue(s.controls[KvV]);
    updated.flashAttention = comboValue(s.controls[FlashAttention]);

    if (updated.mtpSupported) {
        updated.specType = comboValue(s.controls[SpecType]);
        ok &= parseInt(s.controls[SpecDraftNMax], 1, 256, updated.specDraftNMax);
    } else {
        updated.specType = "none";
    }

    if (!ok || updated.kvK.empty() || updated.kvV.empty() || updated.flashAttention.empty()) {
        MessageBoxW(window, L"One or more model settings are invalid.", L"Invalid settings", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (updated.flashAttention != "on" && updated.flashAttention != "off" && updated.flashAttention != "auto") {
        MessageBoxW(window, L"Flash Attention must be on, auto, or off.", L"Invalid Flash Attention setting", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (updated.ubatchSize > updated.batchSize) {
        MessageBoxW(window, L"ubatch-size must not be larger than batch-size.", L"Invalid batch settings", MB_OK | MB_ICONWARNING);
        return false;
    }

    *s.profile = std::move(updated);
    s.saved = true;
    DestroyWindow(window);
    return true;
}

LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* s = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        s = static_cast<State*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (!s) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        auto& p = *s->profile;
        const auto title = L"Profile: " + wide(p.name);
        addStatic(window, 20, 12, 730, 22, title.c_str());
        addStatic(window, 20, 36, 730, 22, L"Saved per profile. Changes take effect on the next Start/Restart.");

        int y = 66;
        addRow(window,*s,Context,y,L"Context (tokens)",std::to_wstring(p.context),L"-c / ctx-size. Working context for chat, tools and files."); y+=36;
        addRow(window,*s,Temperature,y,L"Temperature",number(p.temperature),L"--temp. Lower = more deterministic; higher = more varied."); y+=36;
        addRow(window,*s,TopK,y,L"Top K",std::to_wstring(p.topK),L"--top-k. Keep only K most likely token candidates."); y+=36;
        addRow(window,*s,TopP,y,L"Top P",number(p.topP),L"--top-p. Nucleus sampling probability-mass cutoff."); y+=36;
        addRow(window,*s,PresencePenalty,y,L"Presence penalty",number(p.presencePenalty),L"--presence-penalty. Penalize tokens already seen once."); y+=36;
        addRow(window,*s,RepeatPenalty,y,L"Repeat penalty",number(p.repeatPenalty),L"--repeat-penalty. General repetition penalty; 1.0 = off."); y+=36;
        addRow(window,*s,FrequencyPenalty,y,L"Frequency penalty",number(p.frequencyPenalty),L"--frequency-penalty. Penalty grows with occurrence count."); y+=36;
        addRow(window,*s,BatchSize,y,L"Batch size",std::to_wstring(p.batchSize),L"--batch-size. Logical prompt-processing batch size."); y+=36;
        addRow(window,*s,UBatchSize,y,L"Ubatch size",std::to_wstring(p.ubatchSize),L"--ubatch-size. Physical compute batch; must be <= batch."); y+=36;
        addRow(window,*s,Parallel,y,L"Parallel slots",std::to_wstring(p.parallel),L"-np. Simultaneous server slots; 1 suits our single-user agent."); y+=36;
        addRow(window,*s,GpuLayers,y,L"GPU layers",std::to_wstring(p.gpuLayers),L"-ngl. Requested GPU offload; 999 = as many as possible."); y+=36;
        addRow(window,*s,CpuMoe,y,L"CPU MoE layers",std::to_wstring(p.cpuMoe),L"-ncmoe. MoE expert layers kept in CPU/RAM."); y+=36;

        const wchar_t* kvTypes[] = {L"q8_0",L"q4_0",L"q4_1",L"q5_0",L"q5_1",L"f16",L"bf16",L"f32"};
        addStatic(window,20,y+4,170,20,L"KV cache K");
        s->controls[KvK]=addCombo(window,195,y,120,kvTypes,std::size(kvTypes),wide(p.kvK));
        addStatic(window,330,y+2,420,34,L"-ctk. K-cache type; q8_0 is our current balance."); y+=36;
        addStatic(window,20,y+4,170,20,L"KV cache V");
        s->controls[KvV]=addCombo(window,195,y,120,kvTypes,std::size(kvTypes),wide(p.kvV));
        addStatic(window,330,y+2,420,34,L"-ctv. V-cache type; q8_0 is our current balance."); y+=36;

        const wchar_t* flashModes[] = {L"on",L"auto",L"off"};
        addStatic(window,20,y+4,170,20,L"Flash Attention");
        s->controls[FlashAttention]=addCombo(window,195,y,120,flashModes,std::size(flashModes),wide(p.flashAttention));
        addStatic(window,330,y+2,420,34,L"--flash-attn on/auto/off. ON is our default for Qwen on NVIDIA; AUTO lets llama.cpp decide."); y+=40;

        const wchar_t* specTypes[] = {L"none",L"draft-mtp"};
        addStatic(window,20,y+4,170,20,L"Speculative type");
        s->controls[SpecType]=addCombo(window,195,y,120,specTypes,std::size(specTypes),wide(p.specType));
        addStatic(window,330,y+2,420,34,L"--spec-type. draft-mtp uses MTP heads embedded in the GGUF."); y+=36;
        addRow(window,*s,SpecDraftNMax,y,L"Spec draft N max",std::to_wstring(p.specDraftNMax),L"--spec-draft-n-max. Maximum draft tokens per speculative step."); y+=36;

        const wchar_t* capability = p.mtpSupported
            ? L"MTP: supported by this profile - speculative controls are enabled."
            : L"MTP: not supported by this profile - speculative controls are intentionally disabled.";
        addStatic(window,20,y+2,730,28,capability);
        EnableWindow(s->controls[SpecType],p.mtpSupported?TRUE:FALSE);
        EnableWindow(s->controls[SpecDraftNMax],p.mtpSupported?TRUE:FALSE);
        y+=34;

        CreateWindowW(L"BUTTON",L"Save",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,560,y,85,28,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSave)),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE,655,y,85,28,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancel)),nullptr,nullptr);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam)==kSave && HIWORD(wParam)==BN_CLICKED) { saveValues(window,*s); return 0; }
        if (LOWORD(wParam)==kCancel && HIWORD(wParam)==BN_CLICKED) { DestroyWindow(window); return 0; }
        break;
    case WM_CLOSE: DestroyWindow(window); return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}

bool ensureClass() {
    static bool registered=false;
    if(registered)return true;
    WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=kClassName;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    registered=RegisterClassW(&wc)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
    return registered;
}
}

bool showModelSettings(HWND parent, config::Profile& profile, std::string& error) {
    if(!ensureClass()){error="Could not register Model Settings window class.";return false;}
    State state{&profile};
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,kClassName,L"Model Settings",WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,790,840,parent,nullptr,GetModuleHandleW(nullptr),&state);
    if(!window){error="Could not create Model Settings window.";return false;}

    RECT pr{},wr{};
    if(parent&&GetWindowRect(parent,&pr)&&GetWindowRect(window,&wr)){
        const int width=wr.right-wr.left,height=wr.bottom-wr.top;
        const int x=std::max(10,static_cast<int>(pr.left+((pr.right-pr.left)-width)/2));
        const int y=std::max(10,static_cast<int>(pr.top+((pr.bottom-pr.top)-height)/2));
        SetWindowPos(window,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER);
    }
    if(parent)EnableWindow(parent,FALSE);
    ShowWindow(window,SW_SHOW);UpdateWindow(window);
    MSG msg{};
    while(IsWindow(window)&&GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(parent){EnableWindow(parent,TRUE);SetForegroundWindow(parent);}
    return state.saved;
}
}
