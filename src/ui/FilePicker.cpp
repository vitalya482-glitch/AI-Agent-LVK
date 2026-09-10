#include "ui/FilePicker.h"
#include "util/Text.h"
#include <shobjidl.h>
#include <wrl/client.h>

namespace lvk::ui {
std::optional<std::filesystem::path> pickModelPath(HWND owner,bool folder){
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    HRESULT hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if(SUCCEEDED(hr)){
        DWORD options{};dialog->GetOptions(&options);
        hr=dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR|(folder?FOS_PICKFOLDERS:FOS_FILEMUSTEXIST));
        dialog->SetTitle(folder?L"Choose model installation folder":L"Choose an existing GGUF model");
        if(!folder){const COMDLG_FILTERSPEC filter{L"GGUF models (*.gguf)",L"*.gguf"};dialog->SetFileTypes(1,&filter);}
        if(SUCCEEDED(hr))hr=dialog->Show(owner);
        if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return std::nullopt;
        if(SUCCEEDED(hr)){
            Microsoft::WRL::ComPtr<IShellItem> item;hr=dialog->GetResult(&item);
            if(SUCCEEDED(hr)){
                PWSTR path{};hr=item->GetDisplayName(SIGDN_FILESYSPATH,&path);
                if(SUCCEEDED(hr)){std::filesystem::path result(path);CoTaskMemFree(path);return result;}
            }
        }
    }
    const auto message=util::wide("Cannot open model picker: "+util::winError(static_cast<DWORD>(hr)));
    MessageBoxW(owner,message.c_str(),L"Model picker",MB_OK|MB_ICONERROR);return std::nullopt;
}
}
