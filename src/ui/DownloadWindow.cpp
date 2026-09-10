#include "ui/DownloadWindow.h"
#include "ui/FilePicker.h"
#include "util/Text.h"
#include <commctrl.h>
#include <iomanip>
#include <sstream>

namespace lvk::ui {
namespace {
constexpr UINT kProgress=WM_APP+100,kComplete=WM_APP+101;
constexpr int kBrowse=10,kDownload=11,kCancel=12,kModel=13;
HWND control(HWND window,const wchar_t* type,const wchar_t* text,int x,int y,int width,int height,DWORD style=0,int id=0){
    HWND child=CreateWindowW(type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return child;
}
int choice(HWND owner,const wchar_t* title,const wchar_t* text,const wchar_t* first,const wchar_t* second){
    const TASKDIALOG_BUTTON buttons[]={{100,first},{101,second}};
    TASKDIALOGCONFIG config{sizeof(config)};config.hwndParent=owner;config.pszWindowTitle=title;config.pszMainInstruction=text;
    config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;config.dwCommonButtons=TDCBF_CANCEL_BUTTON;
    config.cButtons=2;config.pButtons=buttons;config.nDefaultButton=IDCANCEL;
    int selected=IDCANCEL;TaskDialogIndirect(&config,&selected,nullptr,nullptr);return selected;
}
}
DownloadWindow::DownloadWindow(HWND parent,Installed installed,std::function<void(const std::string&)> log,std::function<void(const std::filesystem::path&)> rememberFolder):parent_(parent),installed_(std::move(installed)),log_(std::move(log)),rememberFolder_(std::move(rememberFolder)){}
DownloadWindow::~DownloadWindow(){cancel_=true;if(worker_.joinable())worker_.join();if(window_)DestroyWindow(window_);}
void DownloadWindow::show(const std::filesystem::path& lastFolder){
    if(window_){ShowWindow(window_,SW_RESTORE);SetForegroundWindow(window_);return;}
    closeRequested_=false;
    WNDCLASSW klass{};klass.lpfnWndProc=proc;klass.hInstance=GetModuleHandleW(nullptr);klass.lpszClassName=L"AI-Agent-LVK-Download";
    klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&klass);
    window_=CreateWindowExW(WS_EX_DLGMODALFRAME,klass.lpszClassName,L"Download Model",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,700,425,parent_,nullptr,klass.hInstance,this);
    if(!window_){log_("Cannot create Download Model window.");return;}
    SetWindowTextW(folder_,lastFolder.c_str());ShowWindow(window_,SW_SHOW);
}
void DownloadWindow::details(){
    const auto index=SendMessageW(model_,CB_GETCURSEL,0,0);if(index<0)return;
    const auto& entry=models::catalog().at(static_cast<size_t>(index));
    SetWindowTextW(file_,util::wide(entry.filename).c_str());
    std::wostringstream text;text<<std::fixed<<std::setprecision(2)<<static_cast<double>(entry.expectedSize)/1e9<<L" GB ("<<entry.expectedSize<<L" bytes), SHA-256 verified";
    SetWindowTextW(size_,text.str().c_str());
}
void DownloadWindow::begin(){
    if(running_)return;
    if(worker_.joinable())worker_.join();
    cancel_=false;running_=true;
    for(HWND item:{model_,folder_,browse_,download_})EnableWindow(item,FALSE);
    SetWindowTextW(cancelButton_,L"Cancel");
    worker_=std::thread([this]{
        auto result=models::downloadModel(request_,cancel_,[this](const models::DownloadProgress& progress){
            {std::lock_guard lock(mutex_);latest_=progress;}PostMessageW(window_,kProgress,0,0);
        });
        {std::lock_guard lock(mutex_);result_=std::move(result);}
        PostMessageW(window_,kComplete,0,0); // HWND lives until this completion is handled.
    });
}
void DownloadWindow::completed(){
    // Work is already finished; join only reaps the thread after its final message.
    worker_.join();running_=false;
    models::DownloadResult result;{std::lock_guard lock(mutex_);result=result_;}
    if(!result.path.empty())rememberFolder_(request_.directory);
    if(!closeRequested_&&result.status==models::DownloadStatus::NeedExistingChoice){
        const int selected=choice(window_,L"GGUF already exists",L"This GGUF already exists in the selected folder.",L"Use Existing (verify size and SHA-256)",L"Re-download (replace only after verification)");
        if(selected==100||selected==101){request_.existing=selected==100?models::ExistingPolicy::UseExisting:models::ExistingPolicy::Replace;begin();return;}
        result.message="Download cancelled. Existing file was not changed.";
    }else if(!closeRequested_&&result.status==models::DownloadStatus::NeedPartialChoice){
        const int selected=MessageBoxW(window_,L"A .part file exists. Restart will discard its incomplete contents.\n\nRestart download? (Resume is not available yet.)",L"Partial download",MB_OKCANCEL|MB_ICONQUESTION|MB_DEFBUTTON2);
        if(selected==IDOK){request_.restartPartial=true;begin();return;}
        result.message="Download cancelled. Partial file was not changed.";
    }
    if(result.status==models::DownloadStatus::Complete)installed_(models::fromCatalog(request_.entry,result.path),request_.directory);
    if(!result.message.empty()){log_(result.message);SetWindowTextW(status_,util::wide(result.message).c_str());}
    SendMessageW(progress_,PBM_SETMARQUEE,FALSE,0);
    for(HWND item:{model_,folder_,browse_,download_})EnableWindow(item,TRUE);
    SetWindowTextW(cancelButton_,L"Close");
    if(closeRequested_)DestroyWindow(window_);
}
void DownloadWindow::cancelAndClose(){
    closeRequested_=true;cancel_=true;
    if(!window_)return;
    if(pickerOpen_)return; // Preserve the owner until IFileDialog::Show unwinds.
    if(running_){SetWindowTextW(status_,L"Cancelling... Waiting for the current I/O operation to finish.");EnableWindow(cancelButton_,FALSE);}
    else DestroyWindow(window_);
}
void DownloadWindow::renderProgress(){
    models::DownloadProgress value;{std::lock_guard lock(mutex_);value=latest_;}
    const bool determinate=value.total!=0;
    auto style=GetWindowLongPtrW(progress_,GWL_STYLE);
    SetWindowLongPtrW(progress_,GWL_STYLE,determinate?(style&~PBS_MARQUEE):(style|PBS_MARQUEE));
    SendMessageW(progress_,PBM_SETMARQUEE,determinate?FALSE:TRUE,50);
    const auto percent=determinate?static_cast<int>(std::min(100.0,100.0*static_cast<double>(value.bytes)/static_cast<double>(value.total))):0;
    if(determinate)SendMessageW(progress_,PBM_SETPOS,percent,0);
    std::wostringstream text;text<<util::wide(value.phase)<<L"\r\n"<<std::fixed<<std::setprecision(2)<<static_cast<double>(value.bytes)/1e9<<L" GB";
    if(determinate)text<<L" / "<<static_cast<double>(value.total)/1e9<<L" GB  ("<<percent<<L"%)";
    text<<L"\r\nSpeed: "<<value.bytesPerSecond/1e6<<L" MB/s   Elapsed: "<<static_cast<int>(value.seconds)/60<<L":"<<std::setw(2)<<std::setfill(L'0')<<static_cast<int>(value.seconds)%60;
    if(!cancel_)SetWindowTextW(status_,text.str().c_str());
}
LRESULT CALLBACK DownloadWindow::proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto* self=reinterpret_cast<DownloadWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<DownloadWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(window,message,wp,lp);
    switch(message){
    case WM_CREATE:
        control(window,L"STATIC",L"Model:",20,20,100,24);
        self->model_=control(window,L"COMBOBOX",L"",125,17,525,220,CBS_DROPDOWNLIST|WS_TABSTOP,kModel);
        for(const auto& entry:models::catalog())SendMessageW(self->model_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(util::wide(entry.displayName).c_str()));
        SendMessageW(self->model_,CB_SETCURSEL,0,0);
        control(window,L"STATIC",L"File:",20,57,100,24);self->file_=control(window,L"STATIC",L"",125,57,530,24);
        control(window,L"STATIC",L"Expected size:",20,90,105,24);self->size_=control(window,L"STATIC",L"",125,90,530,24);
        control(window,L"STATIC",L"Install folder:",20,128,105,24);
        self->folder_=control(window,L"EDIT",L"",125,125,420,26,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);
        self->browse_=control(window,L"BUTTON",L"Browse...",553,125,100,28,WS_TABSTOP,kBrowse);
        self->status_=control(window,L"STATIC",L"Choose a folder, then confirm Download. No files are downloaded until you do.",20,172,635,95);
        self->progress_=control(window,PROGRESS_CLASSW,L"",20,279,635,23);
        self->download_=control(window,L"BUTTON",L"Download",430,328,110,30,WS_TABSTOP,kDownload);
        self->cancelButton_=control(window,L"BUTTON",L"Cancel",548,328,110,30,WS_TABSTOP,kCancel);
        self->details();return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==kModel&&HIWORD(wp)==CBN_SELCHANGE){self->details();return 0;}
        if(HIWORD(wp)!=BN_CLICKED)break;
        if(LOWORD(wp)==kBrowse){self->pickerOpen_=true;const auto path=pickModelPath(window,true);self->pickerOpen_=false;
            if(self->closeRequested_)DestroyWindow(window);else if(path){SetWindowTextW(self->folder_,path->c_str());self->rememberFolder_(*path);}return 0;}
        if(LOWORD(wp)==kCancel){self->cancelAndClose();return 0;}
        if(LOWORD(wp)==kDownload&&!self->running_){
            std::wstring folder(static_cast<size_t>(GetWindowTextLengthW(self->folder_))+1,L'\0');GetWindowTextW(self->folder_,folder.data(),static_cast<int>(folder.size()));folder.resize(wcslen(folder.c_str()));
            if(folder.empty()){MessageBoxW(window,L"Choose an install folder before downloading.",L"Install folder",MB_OK|MB_ICONINFORMATION);return 0;}
            const auto index=SendMessageW(self->model_,CB_GETCURSEL,0,0);if(index<0)return 0;
            self->request_={models::catalog().at(static_cast<size_t>(index)),std::filesystem::path(folder)};
            self->closeRequested_=false;self->begin();return 0;
        }
        break;
    case kProgress:self->renderProgress();return 0;
    case kComplete:self->completed();return 0;
    case WM_CLOSE:self->cancelAndClose();return 0;
    case WM_NCDESTROY:self->window_=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
    }
    return DefWindowProcW(window,message,wp,lp);
}
}
