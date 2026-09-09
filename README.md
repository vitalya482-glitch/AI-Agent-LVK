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

On first start the launcher creates `config.json` beside the executable. Edit it to point at the installed llama command, model, and workspace:

```json
{
  "llama_command": "llama",
  "server_host": "127.0.0.1",
  "server_port": 8080,
  "workspace": "G:\\AI\\workspace",
  "docker_image": "ai-cpp-sandbox",
  "auto_start_server": false,
  "selected_profile": "Qwen3-Coder-30B-A3B",
  "profiles": [
    {
      "name": "Qwen3-Coder-30B-A3B",
      "model": "G:\\AI\\models\\Qwen3-Coder-30B-A3B\\Qwen3-Coder-30B-A3B-Instruct-Q4_K_M.gguf",
      "context": 16384,
      "parallel": 1,
      "gpu_layers": 999,
      "cpu_moe": 27,
      "kv_k": "q8_0",
      "kv_v": "q8_0",
      "flash_attention": true,
      "tools": "all",
      "tools_runtime": "docker:ai-cpp-sandbox"
    }
  ]
}
```

`llama_command` may also be a full path such as `G:\\AI\\llama.cpp\\llama.exe`. The default bind address is deliberately loopback-only.

The default `Qwen3-Coder-30B-A3B` profile uses a 16384-token context. When an existing `config.json` still has the original default value of 8192 for that profile, the launcher migrates only that context field and preserves the rest of the file. Other user-selected context values are left unchanged.

The GUI also shows system RAM/VRAM and the RAM/VRAM reported for the llama serve process. NVIDIA VRAM is read through the optional driver-provided NVML library; if NVML is unavailable, the launcher shows `VRAM: unavailable` without starting `nvidia-smi` or another helper process.

## Docker sandbox

The repository contains `docker\Dockerfile`. Build it from the launcher directory with the GUI button **Rebuild Sandbox**, or run:

```bat
docker build -t ai-cpp-sandbox docker
```

Docker is used only as the tools sandbox. Inference and CUDA remain native Windows processes.

## Using the launcher

1. Start `AI-Agent-LVK.exe`.
2. Select a profile.
3. Make sure the status panel reports `llama`, Docker, the image, model, workspace, and port correctly.
4. Press **Start AI**.
5. Press **Open Web UI** to open `http://127.0.0.1:8080`.
6. Use **Stop** or **Restart** to control only the process launched by this window.

Logs are shown in the window and written to `logs\launcher.log`; llama stdout/stderr is appended there as well. The workspace is explicit and is not the repository root by default.

## Architecture

`ConfigManager`, `DependencyChecker`, `DockerManager`, `LlamaManager`, `ProcessManager`, `PortChecker`, and `LogManager` are native C++ modules behind a simple Win32 GUI. There is no custom HTTP API, command dispatcher, embedded model engine, agent loop, MCP client, or duplicate tool runtime.

## Updates

`LVKUpdater.exe`, `app.update.json`, release packaging, SHA-256 calculation, and `update/manifest.json` generation remain part of the existing release flow. Releases are created from semantic version tags `vX.Y.Z`.
