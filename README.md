# AI-Agent-LVK

AI-Agent-LVK is a small Windows launcher and manager for local AI based on the ready-made `llama.cpp` server. It does not run inference itself and does not implement an agent framework. It manages three external pieces: `llama`, a GGUF model, and a Docker sandbox used by llama.cpp tools.

The launcher is a single native Win32 executable. It checks dependencies, starts `llama serve` with a selected profile, captures stdout/stderr, monitors `127.0.0.1:8080`, and opens the llama.cpp Web UI. The updater integration remains available.

## Requirements

- Windows 11 x64
- `llama` from llama.cpp, either on `PATH` or as an absolute path
- Docker Desktop with a running Docker Engine
- A GGUF model
- NVIDIA GPU/CUDA is recommended for the current Qwen3-Coder profile, but the launcher itself does not require CUDA

## Build

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

The output is `build\Release\AI-Agent-LVK.exe`. The build also copies `app.update.json` and the `docker` folder beside it.

## Configuration

On first start the launcher creates `config.json` beside the executable. The current coding profile stores runtime and sampling tuning per model:

```json
{
  "name": "Qwen3-Coder-30B-A3B",
  "model": "G:\\AI\\models\\Qwen3-Coder-30B-A3B\\Qwen3-Coder-30B-A3B-Instruct-Q4_K_M.gguf",
  "context": 32768,
  "parallel": 1,
  "gpu_layers": 999,
  "cpu_moe": 27,
  "temperature": 0.3,
  "top_k": 20,
  "top_p": 0.95,
  "presence_penalty": 0.0,
  "repeat_penalty": 1.0,
  "frequency_penalty": 0.0,
  "batch_size": 512,
  "ubatch_size": 253,
  "kv_k": "q8_0",
  "kv_v": "q8_0",
  "flash_attention": true,
  "mtp_supported": false,
  "spec_type": "none",
  "spec_draft_n_max": 2,
  "tools": "all",
  "tools_runtime": "docker:ai-cpp-sandbox"
}
```

The default `Qwen3-Coder-30B-A3B` profile uses a 32768-token context. Existing configs that still have the earlier stock values 8192 or 16384 for that profile are migrated to 32768; other user-selected context values are preserved.

## Model Settings page

The main window has a **Model Settings** button. It opens a native Win32 page for all currently discussed tuning parameters: context, temperature, top-k, top-p, presence/repeat/frequency penalties, batch size, ubatch size, parallel slots, GPU layers, CPU MoE layers, K/V cache types, Flash Attention, and speculative-decoding settings. Each field includes a short description of what it controls. Settings are saved per model profile and apply on the next Start/Restart.

The current coding defaults are:

- `temperature = 0.3`
- `top_k = 20`
- `top_p = 0.95`
- `presence_penalty = 0.0`
- `repeat_penalty = 1.0`
- `frequency_penalty = 0.0`
- `batch_size = 512`
- `ubatch_size = 253`

The repetition-related penalties are intentionally neutral for coding because source code naturally repeats identifiers, keywords, paths, JSON keys, and command fragments.

### MTP speculative decoding

Profiles have an explicit `mtp_supported` capability flag. For a normal model such as the current `Qwen3-Coder-30B-A3B` profile it is `false`; the Model Settings page shows `--spec-type` and `--spec-draft-n-max` disabled/greyed out and the launcher does not pass MTP flags to `llama serve`.

For a known MTP-capable GGUF such as a future `Qwen3.6-35B-A3B MTP` profile, set:

```json
"mtp_supported": true,
"spec_type": "draft-mtp",
"spec_draft_n_max": 2
```

The controls then become editable and the launcher passes:

```text
--spec-type draft-mtp --spec-draft-n-max 2
```

This prevents accidentally requesting MTP from a GGUF that does not contain compatible MTP heads.

## Memory monitor

The GUI shows system RAM/VRAM and the RAM/VRAM reported for the launcher-owned `llama serve` process. NVIDIA VRAM is read through the optional driver-provided NVML library; if NVML is unavailable, the launcher reports it as unavailable instead of starting a helper process.

## Docker sandbox

The repository contains `docker\Dockerfile`. Build it from the launcher directory with **Rebuild Sandbox**, or run:

```bat
docker build -t ai-cpp-sandbox docker
```

Docker is used only as the tools sandbox. Inference and CUDA remain native Windows processes.

## Using the launcher

1. Start `AI-Agent-LVK.exe`.
2. Select a profile.
3. Optionally open **Model Settings** and tune the selected profile.
4. Make sure the status panel reports `llama`, Docker, the image, model, workspace, and port correctly.
5. Press **Start AI**.
6. Press **Open Web UI** to open `http://127.0.0.1:8080`.
7. Use **Stop** or **Restart** to control only the process launched by this window.

Logs are shown in the window and written to `logs\launcher.log`; llama stdout/stderr is appended there as well.

## Architecture

`ConfigManager`, `DependencyChecker`, `DockerManager`, `LlamaManager`, `ProcessManager`, `PortChecker`, `LogManager`, and the native Model Settings page are Win32/C++ modules. There is no custom HTTP API, command dispatcher, embedded model engine, agent loop, MCP client, or duplicate tool runtime.

## Updates

`LVKUpdater.exe`, `app.update.json`, release packaging, SHA-256 calculation, and `update/manifest.json` generation remain part of the existing release flow. Releases are created from semantic version tags `vX.Y.Z`.
