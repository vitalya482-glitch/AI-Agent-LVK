# AI-Agent-LVK — Project Context

The project was deliberately reduced to a thin Windows utility around existing local-AI components.

## Product definition

AI-Agent-LVK is a native Win32 launcher and manager for `llama.cpp`, Docker, and GGUF models. It does not load GGUF weights, perform inference, expose a custom API, or implement an agent/tool/MCP framework. Those responsibilities belong to llama.cpp.

## Runtime flow

The GUI loads `config.json`, checks `llama`, Docker CLI/Engine, the configured Docker image, the selected model, workspace, and localhost port. Docker and the sandbox image are prerequisites for llama.cpp tools, but they do not block the native `llama serve` model startup. `Start Docker` launches Docker Desktop hidden and waits for Engine readiness; `Start AI` may request Docker startup in the background and continues with the model server. `LlamaManager` builds a profile-based `llama serve` command and `ProcessManager` launches it directly with `CreateProcessW`, hidden window, and redirected output. `PortChecker` verifies readiness. The GUI can stop/restart that exact process, show logs, open the Web UI, rebuild the sandbox, open configured folders, and edit model tuning through a native Model Settings page.

The default endpoint is `127.0.0.1:8080`. On first run the workspace is beside
the launcher at `<launcher-directory>\workspace` when writable, otherwise
`%LOCALAPPDATA%\AI-Agent-LVK\workspace`. Existing configured paths are preserved.
Tools use an owned `docker-container:<id>` runtime created from the configured
`ai-cpp-sandbox` image; the launcher never gives a model arbitrary Windows shell
access.

## Portable Docker workspace

`ConfigManager` persists the host directory as `workspace_path` (and accepts the
old `workspace` key during migration). **Change Workspace** updates it and
**Open Workspace** opens the same directory used by Start AI. Before launch,
`DockerManager` creates a launcher-owned container with the effective shape:

```text
docker create --read-only --tmpfs /tmp --tmpfs /root/.cache --cap-drop ALL \
  --security-opt no-new-privileges -v <workspace>:/workspace -w /workspace \
  ai-cpp-sandbox sleep infinity
```

The container receives `AGENT_WORKSPACE=/workspace`; persistent source files,
build outputs and deliverables belong under `/workspace`, while `/tmp` is
temporary. No Docker socket, privileged flag, or broad host mount is used.
Rebuilding the image does not touch the host workspace.

## Configuration and profiles

`ConfigManager` parses strict UTF-8 JSON in `config.json` beside the executable. Schema 2 stores `installed_models`, `active_model_id` and `last_model_download_directory`, plus existing global launcher settings. The C++ Profile is the installed-model record: stable ID, display name, absolute Unicode path, family/quant, capability metadata and every per-model tuning value. Legacy profiles and single model_path configurations migrate without resetting values; exact original bytes are retained in config.json.pre-models.bak. Malformed JSON is never silently replaced.

The default `Qwen3-Coder-30B-A3B` profile currently uses:

- context: 32768 tokens by default;
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

MTP availability is explicit capability data, never inferred from filenames. Controls and flags require both `mtp_supported=true` and an existing separate `mtp_model_path`. The background scanner caches file availability; launch revalidates it. Without both requirements, the stored spec settings are preserved but not emitted. Qwen3.6's main catalog GGUF is MTP-disabled. Future explicit draft configurations use `--model-draft`, `--spec-type` and `--spec-draft-n-max`.

Sampling settings are per-profile and passed directly to llama.cpp. `temperature` uses `--temp`, `top_k` uses `--top-k`, and `top_p` uses `--top-p`. Keep them independently configurable because future model profiles may need different recommended values.

The current coding profile keeps the three conventional repetition penalties neutral: `presence_penalty = 0.0`, `repeat_penalty = 1.0`, and `frequency_penalty = 0.0`. These are passed to llama.cpp as `--presence-penalty`, `--repeat-penalty`, and `--frequency-penalty`. Neutral values are intentional because program code naturally repeats identifiers, syntax, types, JSON keys, file paths, and command fragments.

Prompt processing uses `batch_size = 512` and `ubatch_size = 253`, passed to llama.cpp as `--batch-size` and `--ubatch-size`. Batch size is the logical prompt-processing batch limit; micro-batch size is the physical chunk size used during evaluation. Both are performance/memory tuning parameters and belong in the model profile rather than being hardcoded.

Migration preserves ALL existing context values, including 8192 and 16384. The prior stock-context promotion has been removed. The existing Qwen3-Coder model remains active after migration.

Future launcher tuning work should continue exposing model/runtime parameters through profiles rather than hardcoding them. Parameters discussed during model tuning should be documented with a short explanation of what they control and should remain independently configurable per model profile.

The Win32 GUI includes a background memory monitor. It uses `GlobalMemoryStatusEx` for system RAM, `GetProcessMemoryInfo` for the launcher-owned llama process, and optional dynamically loaded NVML for GPU memory. Missing NVML is reported as unavailable without an external helper process.

## Updater

The LVK-Updater integration remains intentionally separate. `app.update.json` is generated by CMake, `UpdateManager` starts `LVKUpdater.exe`, and the hidden Win32 close bridge allows the updater to request shutdown after package verification. The main executable name remains `AI-Agent-LVK.exe`.

## Excluded architecture

Do not restore the old console core, embedded llama.cpp submodule build, custom socket HTTP server, `/api/v1` endpoints, `CommandDispatcher`, `ModelRuntime`, GUI API client, agent loop, native tool registry, RAG, or MCP implementation. They were removed because they duplicated llama.cpp or were outside the launcher scope.

## Model catalog, registration and download

`src/models/ModelCatalog.*` is separate from installed-model storage and the main window. Its first entry is Qwen3.6-35B-A3B Q4_K_M from ggml-org on Hugging Face: 20,419,565,568 bytes with verified SHA-256. Recommended launcher context is 8192 and the current profile permits discrete values through the documented 262144-token model context window; MTP is false. CPU MoE 27 is only an editable initial value, not a performance guarantee. Unknown manually added GGUFs also start conservatively; existing model settings are never reset.

Add Existing uses a modern IFileOpenDialog GGUF picker and worker validation, with filename-derived display names and no data copying. Download Model uses a modeless Win32 DownloadWindow and folder picker (IFileDialog / FOS_PICKFOLDERS). A visible path and explicit Download are required every time. Missing entries stay registered with [Missing]; Locate changes only the path, Remove from List changes only registration, and Open Model Folder uses the selected entry. Changing selection while running requires Restart to apply it.

ModelDownloader is an independently testable worker-only service. Preflight checks directory, write access, disk reserve, final-file conflicts and partial-file conflicts. Use Existing verifies the full file; Re-download preserves the old final until successful commit. Partial Resume is not yet implemented, but policy is separate from transport for later Range support. Restart of a partial requires explicit consent.

WinHTTP native async requests run under an owned thread. UI updates arrive via PostMessage at approximately 150 ms intervals. Cancellation is polled while awaiting network completion, closes the async handle, and retains callback state/read buffers until HANDLE_CLOSING. Streaming .part writes, GGUF magic, Content-Length/catalog-size verification and optional BCrypt SHA-256 all precede flush, close and same-directory rename. Only a successful final file is registered. HTTPS certificate validation and downgrade rejection are preserved. Numeric localhost HTTP is permitted solely for native test fixtures.

The main window stays alive during cancellation; download threads are never detached. The updater close bridge routes shutdown to the main window of its OWN process, so downloads and config writes can finish safely. Llama handles are controlled by the serialized worker; output has its own reader and a stale exited child is reaped before another model is started. Native tests are enabled with LVK_BUILD_TESTS=ON and excluded from the release package.
