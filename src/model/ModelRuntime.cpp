#include "model/ModelRuntime.h"
#include "llama.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace lvk::model {
struct ModelRuntime::Impl { llama_model* model = nullptr; llama_context* context = nullptr; ModelConfig config; std::string path; std::string description; };
namespace { int threads(int value) { const auto count = std::thread::hardware_concurrency(); return value > 0 ? value : static_cast<int>(std::max(1U, count == 0 ? 1U : count / 2)); } }
ModelRuntime::ModelRuntime() : impl_(std::make_unique<Impl>()) { llama_backend_init(); }
ModelRuntime::~ModelRuntime() { unload(); llama_backend_free(); }
ModelResult ModelRuntime::configure(ModelConfig config) {
    if (impl_->model) return {false, "Unload the current model before changing configuration."};
    if (config.contextSize < 128 || config.threadCount < 0 || config.gpuLayers < 0) return {false, "Invalid model configuration."};
    if (config.gpuLayers > 0 && !llama_supports_gpu_offload()) return {false, "This build has no GPU backend."};
    impl_->config = config; return {true, "Model configuration updated."};
}
ModelResult ModelRuntime::load(const std::string& path) {
    unload(); std::error_code ec;
    if (!fs::is_regular_file(fs::path(path), ec)) return {false, "GGUF file was not found: " + path};
    auto params = llama_model_default_params(); params.n_gpu_layers = impl_->config.gpuLayers;
    impl_->model = llama_model_load_from_file(path.c_str(), params);
    if (!impl_->model) return {false, "llama.cpp could not load this GGUF model."};
    auto context = llama_context_default_params(); context.n_ctx = static_cast<uint32_t>(impl_->config.contextSize); context.n_batch = std::min<uint32_t>(context.n_ctx, 512); context.n_ubatch = context.n_batch; context.n_threads = threads(impl_->config.threadCount); context.n_threads_batch = context.n_threads;
    impl_->context = llama_init_from_model(impl_->model, context);
    if (!impl_->context) { llama_model_free(impl_->model); impl_->model = nullptr; return {false, "llama.cpp could not create an inference context."}; }
    std::array<char, 512> desc{}; impl_->description = llama_model_desc(impl_->model, desc.data(), desc.size()) > 0 ? desc.data() : "Unknown GGUF model"; impl_->path = path;
    return {true, "Model loaded: " + impl_->description};
}
void ModelRuntime::unload() { if (impl_->context) { llama_free(impl_->context); impl_->context = nullptr; } if (impl_->model) { llama_model_free(impl_->model); impl_->model = nullptr; } impl_->path.clear(); impl_->description.clear(); }
ModelStatus ModelRuntime::status() const {
    ModelStatus result;
    result.loaded = impl_->model != nullptr;
    result.gpuAvailable = llama_supports_gpu_offload();
    result.path = impl_->path;
    result.description = impl_->description;
    result.config = impl_->config;
    if (!impl_->model) return result;
    result.modelSizeBytes = llama_model_size(impl_->model);
    result.parameterCount = llama_model_n_params(impl_->model);
    result.modelLayers = llama_model_n_layer(impl_->model);
    const int weightSections = std::max(1, result.modelLayers + 2);
    result.gpuLayersLoaded = result.gpuAvailable ? std::min(impl_->config.gpuLayers, weightSections) : 0;
    result.gpuWeightBytesEstimate = result.modelSizeBytes * static_cast<std::uint64_t>(result.gpuLayersLoaded) / static_cast<std::uint64_t>(weightSections);
    result.cpuWeightBytesEstimate = result.modelSizeBytes - result.gpuWeightBytesEstimate;
    if (impl_->context) {
        result.contextTokensUsed = static_cast<int>(std::max<llama_pos>(0, llama_memory_seq_pos_max(llama_get_memory(impl_->context), 0)));
    }
    return result;
}
ModelResult ModelRuntime::chat(const std::string& message, std::string& response, int maxTokens) {
    response.clear(); if (!impl_->context || message.empty()) return {false, impl_->context ? "Message is empty." : "No model is loaded."};
    const auto* vocab = llama_model_get_vocab(impl_->model); std::string prompt = message;
    if (const char* tmpl = llama_model_chat_template(impl_->model, nullptr)) { const llama_chat_message chat{"user", message.c_str()}; const int32_t needed = llama_chat_apply_template(tmpl, &chat, 1, true, nullptr, 0); if (needed <= 0) return {false, "Could not apply the model chat template."}; std::vector<char> out(static_cast<size_t>(needed) + 1); const int32_t written = llama_chat_apply_template(tmpl, &chat, 1, true, out.data(), static_cast<int32_t>(out.size())); if (written < 0) return {false, "Could not format the model chat prompt."}; prompt.assign(out.data(), static_cast<size_t>(written)); }
    const int count = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), nullptr, 0, true, true); if (count >= 0) return {false, "Could not tokenize the prompt."}; std::vector<llama_token> tokens(static_cast<size_t>(-count)); if (llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(), static_cast<int32_t>(tokens.size()), true, true) < 0 || tokens.size() + static_cast<size_t>(maxTokens) > impl_->config.contextSize) return {false, "Prompt exceeds the configured context."};
    llama_memory_clear(llama_get_memory(impl_->context), true); if (llama_decode(impl_->context, llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()))) != 0) return {false, "Prompt evaluation failed."}; auto* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params()); llama_sampler_chain_add(sampler, llama_sampler_init_greedy()); llama_token next = llama_sampler_sample(sampler, impl_->context, -1);
    for (int i = 0; i < maxTokens && !llama_vocab_is_eog(vocab, next); ++i) { char piece[512]; const int size = llama_token_to_piece(vocab, next, piece, sizeof(piece), 0, true); if (size < 0 || llama_decode(impl_->context, llama_batch_get_one(&next, 1)) != 0) { llama_sampler_free(sampler); return {false, "Generation failed."}; } response.append(piece, static_cast<size_t>(size)); next = llama_sampler_sample(sampler, impl_->context, -1); }
    llama_sampler_free(sampler); return {true, "Response generated."};
}
} // namespace lvk::model
