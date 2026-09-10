#include "ui/ModelSettingsWindow.h"
#include "util/Text.h"
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace lvk::ui {
namespace {
constexpr wchar_t kClassName[] = L"AI-Agent-LVK-ModelSettings";
constexpr int kSave = 9001, kCancel = 9002;

constexpr std::array<int, 13> kContextSizes = {
    512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152
};

struct SamplingPreset {
    const wchar_t* name;
    double temperature;
    int topK;
    double topP;
    double presencePenalty;
    double repeatPenalty;
    double frequencyPenalty;
};

constexpr std::array<SamplingPreset, 3> kSamplingPresets = {{
    {L"Coding", 0.30, 20, 0.95, 0.00, 1.00, 0.00},
    {L"Creative", 0.80, 50, 0.95, 0.30, 1.05, 0.20},
    {L"Chaos / hallucination", 1.30, 100, 1.00, 0.80, 1.10, 0.50},
}};

enum Field : size_t {
    Context, Temperature, TopK, TopP, PresencePenalty, RepeatPenalty,
    FrequencyPenalty, BatchSize, UBatchSize, Parallel, GpuLayers, CpuMoe,
    KvK, KvV, FlashAttention, AgentTurnLimit, SpecType, SpecDraftNMax, FieldCount
};

struct State {
    config::Profile* profile{};
    std::array<HWND, FieldCount> controls{};
    HWND presetCombo{};
    HWND contextDescription{};
    bool applyingPreset = false;
    bool saved = false;
    bool contextChanged = false;
};

std::wstring wide(const std::string& s) { return util::wide(s); }
std::string narrow(const std::wstring& s) { return util::utf8(s); }

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
        x, y, w, 220, parent, nullptr, nullptr, nullptr);
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

bool same(double a, double b) {
    return std::abs(a - b) < 0.000001;
}

bool matchesPreset(const config::Profile& p, const SamplingPreset& preset) {
    return same(p.temperature, preset.temperature) && p.topK == preset.topK &&
        same(p.topP, preset.topP) && same(p.presencePenalty, preset.presencePenalty) &&
        same(p.repeatPenalty, preset.repeatPenalty) && same(p.frequencyPenalty, preset.frequencyPenalty);
}

std::wstring currentPresetName(const config::Profile& p) {
    for (const auto& preset : kSamplingPresets) {
        if (matchesPreset(p, preset)) return preset.name;
    }
    return L"Custom";
}

const SamplingPreset* findPreset(const std::wstring& name) {
    for (const auto& preset : kSamplingPresets) {
        if (name == preset.name) return &preset;
    }
    return nullptr;
}

void setEdit(HWND control, const std::wstring& value) {
    SetWindowTextW(control, value.c_str());
}

void applyPreset(State& s, const SamplingPreset& preset) {
    s.applyingPreset = true;
    setEdit(s.controls[Temperature], number(preset.temperature));
    setEdit(s.controls[TopK], std::to_wstring(preset.topK));
    setEdit(s.controls[TopP], number(preset.topP));
    setEdit(s.controls[PresencePenalty], number(preset.presencePenalty));
    setEdit(s.controls[RepeatPenalty], number(preset.repeatPenalty));
    setEdit(s.controls[FrequencyPenalty], number(preset.frequencyPenalty));
    s.applyingPreset = false;
}

bool isSamplingControl(const State& s, HWND control) {
    return control == s.controls[Temperature] || control == s.controls[TopK] ||
        control == s.controls[TopP] || control == s.controls[PresencePenalty] ||
        control == s.controls[RepeatPenalty] || control == s.controls[FrequencyPenalty];
}

void selectCustomPreset(State& s) {
    if (s.applyingPreset || !s.presetCombo) return;
    SendMessageW(s.presetCombo, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(L"Custom"));
}

int contextMaxIndex(const config::Profile& p) {
    int result = 0;
    for (size_t i = 0; i < kContextSizes.size(); ++i) {
        if (kContextSizes[i] <= p.maxContextCapability()) result = static_cast<int>(i);
    }
    return result;
}

int nearestContextIndex(int value, int maxIndex) {
    int best = 0;
    long long bestDistance = std::llabs(static_cast<long long>(value) - kContextSizes[0]);
    for (int i = 1; i <= maxIndex; ++i) {
        const long long distance = std::llabs(static_cast<long long>(value) - kContextSizes[static_cast<size_t>(i)]);
        if (distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

std::wstring shortContext(int value) {
    if (value >= 1048576 && value % 1048576 == 0) return std::to_wstring(value / 1048576) + L"M";
    return std::to_wstring(value / 1024) + L"K";
}

void updateContextDescription(State& s) {
    if (!s.controls[Context] || !s.contextDescription || !s.profile) return;
    const int pos = static_cast<int>(SendMessageW(s.controls[Context], TBM_GETPOS, 0, 0));
    const int context = kContextSizes[static_cast<size_t>(std::clamp(pos, 0, static_cast<int>(kContextSizes.size() - 1)))];
    const std::wstring value = shortContext(context) + L" (" + std::to_wstring(context) +
        L" tokens). Discrete steps; profile cap " + shortContext(s.profile->maxContextCapability()) + L".";
    SetWindowTextW(s.contextDescription, value.c_str());
}

bool saveValues(HWND window, State& s) {
    auto updated = *s.profile;
    bool ok = true;
    const int contextPos = static_cast<int>(SendMessageW(s.controls[Context], TBM_GETPOS, 0, 0));
    if (contextPos < 0 || contextPos > contextMaxIndex(updated)) ok = false;
    else if(s.contextChanged) updated.context = kContextSizes[static_cast<size_t>(contextPos)];

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
    const auto agentLimit = comboValue(s.controls[AgentTurnLimit]);
    if (agentLimit == "Off") updated.agentTurnLimit = 0;
    else if (agentLimit == "Unlimited") updated.agentTurnLimit = -1;
    else if (agentLimit == "10" || agentLimit == "20" || agentLimit == "50" || agentLimit == "100") updated.agentTurnLimit = std::stoi(agentLimit);
    else ok = false;

    if (updated.mtpSupported && updated.mtpFileAvailable) {
        updated.specType = comboValue(s.controls[SpecType]);
        ok &= parseInt(s.controls[SpecDraftNMax], 1, 256, updated.specDraftNMax);
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
        addStatic(window, 20, y + 4, 170, 20, L"Sampling preset");
        const wchar_t* presetNames[] = {L"Coding", L"Creative", L"Chaos / hallucination", L"Custom"};
        s->presetCombo = addCombo(window, 195, y, 180, presetNames, std::size(presetNames), currentPresetName(p));
        addStatic(window, 390, y + 2, 360, 34, L"Changes sampling only. Hardware, context, KV and tools stay untouched.");
        y += 40;

        addStatic(window, 20, y + 4, 170, 20, L"Context");
        s->controls[Context] = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            195, y - 2, 120, 30, window, nullptr, nullptr, nullptr);
        const int maxContextIndex = contextMaxIndex(p);
        SendMessageW(s->controls[Context], TBM_SETRANGE, TRUE, MAKELPARAM(0, maxContextIndex));
        SendMessageW(s->controls[Context], TBM_SETTICFREQ, 1, 0);
        SendMessageW(s->controls[Context], TBM_SETPOS, TRUE, nearestContextIndex(p.context, maxContextIndex));
        s->contextDescription = addStatic(window, 330, y + 2, 420, 34, L"");
        updateContextDescription(*s);
        y += 36;

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

        const wchar_t* agentTurnLimits[] = {L"Off",L"10",L"20",L"50",L"100",L"Unlimited"};
        const auto agentLimit = p.agentTurnLimit == 0 ? std::wstring(L"Off") : p.agentTurnLimit < 0 ? std::wstring(L"Unlimited") : std::to_wstring(p.agentTurnLimit);
        addStatic(window,20,y+4,170,20,L"Agent turn limit");
        s->controls[AgentTurnLimit]=addCombo(window,195,y,120,agentTurnLimits,std::size(agentTurnLimits),agentLimit);
        addStatic(window,330,y+2,420,34,L"Web UI agenticMaxTurns. Off leaves the Web UI default; Unlimited uses the supported Infinity value."); y+=36;

        const wchar_t* specTypes[] = {L"none",L"draft-mtp"};
        addStatic(window,20,y+4,170,20,L"Speculative type");
        s->controls[SpecType]=addCombo(window,195,y,120,specTypes,std::size(specTypes),wide(p.specType));
        addStatic(window,330,y+2,420,34,L"--spec-type. Requires explicit capability and a separate MTP draft file."); y+=36;
        addRow(window,*s,SpecDraftNMax,y,L"Spec draft N max",std::to_wstring(p.specDraftNMax),L"--spec-draft-n-max. Maximum draft tokens per speculative step."); y+=36;

        const bool mtpReady=p.mtpSupported&&p.mtpFileAvailable;
        const wchar_t* capability = mtpReady
            ? L"MTP: explicit capability and separate draft file available."
            : L"MTP disabled: requires mtp_supported=true and an existing mtp_model_path.";
        addStatic(window,20,y+2,730,28,capability);
        EnableWindow(s->controls[SpecType],mtpReady?TRUE:FALSE);
        EnableWindow(s->controls[SpecDraftNMax],mtpReady?TRUE:FALSE);
        y+=34;

        CreateWindowW(L"BUTTON",L"Save",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,560,y,85,28,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSave)),nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE,655,y,85,28,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancel)),nullptr,nullptr);
        return 0;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == s->controls[Context]) {
            s->contextChanged=true;
            updateContextDescription(*s);
            return 0;
        }
        break;
    case WM_COMMAND: {
        const HWND source = reinterpret_cast<HWND>(lParam);
        const WORD notification = HIWORD(wParam);
        if (LOWORD(wParam)==kSave && notification==BN_CLICKED) { saveValues(window,*s); return 0; }
        if (LOWORD(wParam)==kCancel && notification==BN_CLICKED) { DestroyWindow(window); return 0; }
        if (source == s->presetCombo && notification == CBN_SELCHANGE) {
            const auto selected = comboValue(s->presetCombo);
            const auto selectedWide = wide(selected);
            if (const auto* preset = findPreset(selectedWide)) applyPreset(*s, *preset);
            return 0;
        }
        if (notification == EN_CHANGE && isSamplingControl(*s, source)) {
            selectCustomPreset(*s);
            return 0;
        }
        break;
    }
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
    INITCOMMONCONTROLSEX commonControls{sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&commonControls)) {
        error = "Could not initialize Windows trackbar controls.";
        return false;
    }
    if(!ensureClass()){error="Could not register Model Settings window class.";return false;}
    State state{&profile};
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,kClassName,L"Model Settings",WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,790,880,parent,nullptr,GetModuleHandleW(nullptr),&state);
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
