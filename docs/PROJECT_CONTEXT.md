# AI-Agent-LVK — Project Context

The project was deliberately reduced to a thin Windows utility around existing local-AI components.

## Product definition

AI-Agent-LVK is a native Win32 launcher and manager for `llama.cpp`, Docker, and GGUF models. It does not load GGUF weights, perform inference, expose a custom API, or implement an agent/tool/MCP framework. Those responsibilities belong to llama.cpp.

## Runtime flow

The GUI loads `config.json`, checks `llama`, Docker CLI/Engine, the configured Docker image, the selected model, workspace, and localhost port. `LlamaManager` builds a profile-based `llama serve` command and `ProcessManager` launches it directly with `CreateProcessW`, hidden window, and redirected output. `PortChecker` verifies readiness. The GUI can stop/restart that exact process, show logs, open the Web UI, rebuild the sandbox, open configured folders, and edit model tuning through a native Model Settings page.

The default endpoint is `127.0.0.1:8080`. `G:\AI\workspace` is the default workspace. Tools are configured to use `docker:ai-cpp-sandbox`; the launcher never gives a model arbitrary Windows shell access.

## Configuration and profiles

`ConfigManager` owns the small JSON-shaped `config.json` beside the executable. Profiles contain model path and llama serve tuning such as context, parallelism, GPU layers, MoE CPU layers, sampling settings, penalties, prompt batching, KV types, Flash Attention mode, speculative decoding capability, and tools runtime. The last selected profile is persisted.

The default `Qwen3-Coder-30B-A3B` profile currently uses:

- context: 32768 tokens;
- temperature: 0.3;
- top-k: 20;
- top-p: 0.95;
- presence penalty: 0.0;
- repeat penalty: 1.0;
- frequency penalty: 0.0;
- batch size: 512;
- micro-batch size: 253;
- parallel slots: 1;
- GPU layers: 999;
- CPU MoE layers: 27;
- K/V cache: q8_0;
- Flash Attention: on;
- MTP supported: false;
- spec type: none;
- spec draft N max: 2;
- tools: all;
- tools runtime: docker:ai-cpp-sandbox.

## Model Settings UI

The main launcher toolbar contains a **Model Settings** button. It opens a native Win32 per-profile editor with short explanations beside every currently discussed tuning parameter: context, temperature, top-k, top-p, presence/repeat/frequency penalties, batch size, ubatch size, parallel slots, GPU layers, CPU MoE layers, K/V cache types, Flash Attention, `spec_type`, and `spec_draft_n_max`. Values are persisted to the selected profile and become active after Start/Restart.

Flash Attention is tri-state profile data: `on`, `auto`, or `off`. The current Qwen3-Coder profile uses `on`. `LlamaManager` emits the explicit long-form argument `--flash-attn <mode>`. Existing configs using the older boolean form remain compatible: `true` is loaded as `on`, `false` as `off`. The GUI exposes all three modes in a drop-down so future profiles can use llama.cpp's `auto` behavior when appropriate.

MTP availability is profile capability data, not inferred blindly from a model name. `mtp_supported = false` keeps speculative controls disabled/grey and forces `spec_type = none`; `LlamaManager` does not emit MTP command-line flags. A known MTP GGUF profile such as a future `Qwen3.6-35B-A3B MTP` should set `mtp_supported = true`. Then the GUI enables `none`/`draft-mtp` selection and `spec_draft_n_max`, and `LlamaManager` may emit `--spec-type draft-mtp --spec-draft-n-max N`.

This gating is intentional so a normal GGUF cannot accidentally be launched with MTP options requiring embedded MTP heads.

Sampling settings are per-profile and passed directly to llama.cpp. `temperature` uses `--temp`, `top_k` uses `--top-k`, and `top_p` uses `--top-p`. Keep them independently configurable because future model profiles may need different recommended values.

The current coding profile keeps the three conventional repetition penalties neutral: `presence_penalty = 0.0`, `repeat_penalty = 1.0`, and `frequency_penalty = 0.0`. These are passed to llama.cpp as `--presence-penalty`, `--repeat-penalty`, and `--frequency-penalty`. Neutral values are intentional because program code naturally repeats identifiers, syntax, types, JSON keys, file paths, and command fragments.

Prompt processing uses `batch_size = 512` and `ubatch_size = 253`, passed to llama.cpp as `--batch-size` and `--ubatch-size`. Batch size is the logical prompt-processing batch limit; micro-batch size is the physical chunk size used during evaluation. Both are performance/memory tuning parameters and belong in the model profile rather than being hardcoded.

Existing Qwen3-Coder configs are migrated from the earlier stock context values 8192 or 16384 to 32768 only when they still match those old defaults; other explicitly chosen context values are preserved.

Future launcher tuning work should continue exposing model/runtime parameters through profiles rather than hardcoding them. Parameters discussed during model tuning should be documented with a short explanation of what they control and should remain independently configurable per model profile.

The Win32 GUI includes a background memory monitor. It uses `GlobalMemoryStatusEx` for system RAM, `GetProcessMemoryInfo` for the launcher-owned llama process, and optional dynamically loaded NVML for GPU memory. Missing NVML is reported as unavailable without an external helper process.

## Updater

The LVK-Updater integration remains intentionally separate. `app.update.json` is generated by CMake, `UpdateManager` starts `LVKUpdater.exe`, and the hidden Win32 close bridge allows the updater to request shutdown after package verification. The main executable name remains `AI-Agent-LVK.exe`.

## Excluded architecture

Do not restore the old console core, embedded llama.cpp submodule build, custom socket HTTP server, `/api/v1` endpoints, `CommandDispatcher`, `ModelRuntime`, GUI API client, agent loop, native tool registry, RAG, or MCP implementation. They were removed because they duplicated llama.cpp or were outside the launcher scope.
