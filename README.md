# AI-Agent-LVK

Native C++ runtime for local GGUF models on Windows. It uses [llama.cpp](https://github.com/ggml-org/llama.cpp) directly—without Python, Ollama, Electron or Docker—and is designed to grow into a modular local agent runtime.

## v0.1.6

- Direct `llama.cpp` integration as a pinned Git submodule.
- Load and run local GGUF models from the console, HTTP API, or GUI.
- Native Win32 control-panel GUI: Core, Model Runtime and Chat/command panels; responsive layout with a minimum window size of 800×800.
- GUI can choose an existing `.gguf` file or download one by direct HTTPS URL into `models/`. This works with GitHub Release assets and direct Hugging Face `resolve` URLs.
- Configure context size, CPU thread count and GPU-offloaded layer count before loading a model.
- CUDA is used automatically when the CUDA Toolkit is installed at CMake configure time; otherwise the same build remains CPU-only.
- Download progress is shown as a percentage directly below the model URL.
- **Open Chat** starts a separate chat window: send normal messages without typing `chat` each time.
- Model Runtime status reports model size, parameter count, layer count, context usage, and CPU/RAM versus GPU/VRAM weight-placement estimates.
- GUI network calls run outside the Windows UI thread. Status polling, commands and model generation no longer freeze the window; chat shows a generation state while the response is pending.

## What it is becoming

```text
Console / Win32 GUI / local API
              |
      CommandDispatcher
              |
        ModelRuntime
              |
          llama.cpp
       CPU RAM / GPU VRAM
```

The next layers are intentionally separate: agent instances with their own prompts, context, permissions and tools; native tools through one registry; then MCP client/server support as an external interoperability boundary. Multiple agents should be able to share loaded model weights where the llama.cpp API permits it. Linux is a future deployment target for a dedicated server once the runtime is mature.

## Build

Requirements: Windows 10/11 x64, Visual Studio Build Tools with C++, CMake 3.20+, and Git with submodule support. CUDA Toolkit is optional.

```bat
git clone --recurse-submodules https://github.com/vitalya482-glitch/AI-Agent-LVK.git
cd AI-Agent-LVK
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -A x64
cmake --build build --config Release
```

If the repository has already been cloned, initialize the pinned llama.cpp source once:

```bat
git submodule update --init --recursive
```

Outputs:

```text
build\Release\AI-Agent-LVK.exe
build\Release\AI-Agent-LVK-GUI.exe
build\Release\app.update.json
```

## GUI

Start `AI-Agent-LVK-GUI.exe` beside the Core executable.

1. Press **Start Core**.
2. In **Model runtime**, set context / threads / GPU layers and press **Apply config**. Set GPU layers to `0` for CPU-only. A CUDA-capable build is required for a value above zero.
3. Press **Choose GGUF** to load a local model, or paste a direct `https://.../*.gguf` link and press **Download GGUF**. Downloads run in the background and are stored next to the app in `models/`.
4. Press **Open Chat** and send normal messages. The bottom input in the main window remains available for diagnostics and commands.
5. Press **Refresh status** for placement and runtime information. CPU/RAM and GPU/VRAM weight figures are clearly labelled estimates; the exact buffer accounting will be expanded as the memory planner matures.

Only download models from sources you trust. This first downloader deliberately accepts direct HTTPS `.gguf` files only; it does not yet verify publisher signatures or checksums.

## Commands and API

```text
help
version
status
ping
model status
model config <context> <threads> <gpu_layers>
model load <path-to-model.gguf>
chat <message>
update
```

The local API binds only to `http://127.0.0.1:7842`:

```text
GET  /api/v1/status
GET  /api/v1/version
POST /api/v1/command
POST /api/v1/chat
```

`POST /api/v1/chat` accepts `{"message":"..."}` and now returns a generated response when a model is loaded.

## Releases and updater

Tagging `vX.Y.Z` triggers the Windows release workflow. It builds both executables, produces `AI-Agent-LVK-win-x64.zip`, calculates SHA-256 and package size, creates the GitHub Release, then updates `update/manifest.json` on `main`. The release workflows fetch llama.cpp recursively.

`LVKUpdater.exe` remains a separate companion executable. It checks updates without closing the Core; the Core closes only after the update is confirmed, downloaded and verified.

## Deliberate boundaries

- Runtime code stays C++.
- llama.cpp is the inference backend, not an HTTP wrapper.
- Internal modules use native C++ interfaces.
- MCP will be used later for external tools and clients, not for internal agent-to-agent calls.
- Remote/mobile access stays disabled until authentication, TLS and tool-permission boundaries exist.
