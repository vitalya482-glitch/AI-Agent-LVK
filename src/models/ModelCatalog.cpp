#include "models/ModelCatalog.h"
#include "util/Text.h"
#include <algorithm>
#include <objbase.h>

namespace lvk::models {
const std::vector<ModelCatalogEntry>& catalog() {
    // The Qwen3.6-35B-A3B model card specifies a 262144-token context window.
    // This is the GUI capability ceiling; actual usable context still depends on
    // available RAM/VRAM and llama.cpp runtime support.
    static const std::vector<ModelCatalogEntry> entries{
        {
            "qwen3.6-35b-a3b-q4km", "Qwen3.6-35B-A3B Q4_K_M", "qwen35moe", "Q4_K_M",
            "Qwen3.6-35B-A3B-Q4_K_M.gguf",
            "https://huggingface.co/ggml-org/Qwen3.6-35B-A3B-GGUF/resolve/main/Qwen3.6-35B-A3B-Q4_K_M.gguf",
            20419565568ULL, "671e47e0ec53c665d048b98c3ecbfd5236b5ca9c3e02ed19fc8f81f7b85140c7",
            8192, 262144, 27, false, ""
        },
        {
            "qwen3.6-27b-q6kl", "Qwen3.6-27B Q6_K_L", "qwen35", "Q6_K_L",
            "Qwen_Qwen3.6-27B-Q6_K_L.gguf",
            "https://huggingface.co/bartowski/Qwen_Qwen3.6-27B-GGUF/resolve/main/Qwen_Qwen3.6-27B-Q6_K_L.gguf",
            24291299840ULL, "95c61f9a30b0e0ada59169177fde2234e6bfd0e477a45cbff52097763286f9f6",
            8192, 262144, 0, false, ""
        }
    };
    return entries;
}
std::string newModelId() {
    GUID id{};
    if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot generate model ID.");
    wchar_t text[40]{};StringFromGUID2(id,text,40);
    return util::utf8(text);
}
std::filesystem::path validateGguf(const std::filesystem::path& input) {
    if(input.empty()||input.wstring().find(L'\0')!=std::wstring::npos)
        throw std::runtime_error("Invalid model path.");
    const auto path=std::filesystem::absolute(input).lexically_normal();
    if(_wcsicmp(path.extension().c_str(),L".gguf")!=0)
        throw std::runtime_error("Select a .gguf file, not a .part or executable.");
    if(!std::filesystem::is_regular_file(path))
        throw std::runtime_error("Model file not found: "+util::pathText(path));
    return path;
}
config::InstalledModel fromCatalog(const ModelCatalogEntry& entry,const std::filesystem::path& path) {
    config::InstalledModel model;
    model.id=newModelId();model.name=entry.displayName;model.model=path;
    model.family=entry.family;model.quant=entry.quant;
    model.context=entry.recommendedContext;model.maxContext=entry.maxContext;
    model.cpuMoe=entry.recommendedCpuMoe;
    model.mtpSupported=entry.mtpSupported;
    return model;
}
config::InstalledModel fromExisting(const std::filesystem::path& input) {
    const auto path=validateGguf(input);
    for(const auto& entry:catalog()){
        if(_wcsicmp(path.filename().c_str(),util::wide(entry.filename).c_str())==0)
            return fromCatalog(entry,path);
    }
    auto model=config::InstalledModel{};
    model.id=newModelId();model.model=path;model.name=util::pathText(path.stem());
    model.context=8192;model.maxContext=8192; // Unknown capabilities are never guessed from the name.
    return model;
}
std::vector<config::InstalledModel> discoverInFolder(const std::filesystem::path& input) {
    const auto folder=std::filesystem::absolute(input).lexically_normal();
    std::error_code ec;
    if(!std::filesystem::is_directory(folder,ec))
        throw std::runtime_error("Model folder does not exist: "+util::pathText(folder));

    std::vector<config::InstalledModel> result;
    std::filesystem::directory_iterator it(folder,std::filesystem::directory_options::skip_permission_denied,ec),end;
    if(ec)throw std::runtime_error("Cannot read model folder: "+util::pathText(folder)+" ("+ec.message()+")");
    while(it!=end){
        const auto entry=*it;
        ec.clear();
        if(entry.is_regular_file(ec)&&!ec&&_wcsicmp(entry.path().extension().c_str(),L".gguf")==0)
            result.push_back(fromExisting(entry.path()));
        it.increment(ec);
        if(ec)ec.clear();
    }
    std::sort(result.begin(),result.end(),[](const auto& left,const auto& right){
        return _wcsicmp(left.model.filename().c_str(),right.model.filename().c_str())<0;
    });
    return result;
}
void registerModel(config::Settings& settings, config::InstalledModel model) {
    for(auto& existing:settings.profiles){
        if(_wcsicmp(existing.model.c_str(),model.model.c_str())==0){
            existing.missing=false;settings.selectedProfile=existing.id;return;
        }
    }
    settings.selectedProfile=model.id;settings.profiles.push_back(std::move(model));
}
}
