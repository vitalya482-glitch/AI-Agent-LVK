# AI-Agent-LVK

AI-Agent-LVK is a small Windows launcher and manager for local AI based on the ready-made `llama.cpp` server. It does not run inference itself and does not implement an agent framework. It manages three external pieces: `llama`, a GGUF model, and a Docker sandbox used by llama.cpp tools.

The launcher is a single native Win32 executable. It checks dependencies, starts `llama serve` with a selected profile, captures stdout/stderr, monitors `127.0.0.1:8080`, and opens the llama.cpp Web UI. The updater integration remains available.

## Requirements

- Windows 11 x64
- `llama` from llama.cpp, either on `PATH` or as an absolute path
- Docker Desktop is optional. **Start AI** launches llama.cpp independently; Docker sandbox controls are grouped under **Docker Settings** and are used only when the sandbox is enabled and connected.
- A GGUF model
- NVIDIA GPU/CUDA is recommended for the current Qwen3-Coder profile, but the launcher itself does not require CUDA

## Build

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -A x64
cmake --build build --config Release
```

The output is `build\Release\AI-Agent-LVK.exe`. The build also copies `app.update.json` and the `docker` folder beside it.

Optional native tests (fixtures only; no 20-GB download):

```bat
cmake -S . -B build-tests -A x64 -DLVK_BUILD_TESTS=ON
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

Tests exercise migration, persistence, Unicode paths, per-model command lines,
owned hidden child processes, real WinHTTP loopback transfers, SHA-256, redirects,
truncation, cancellation, partial restart, overwrite protection and disk preflight.
Plain HTTP is allowed only on numeric `127.0.0.1` for these transport fixtures.
The test executable is not included in release packaging.

## Models and configuration

The toolbar's ComboBox lists **registered installed models**, not the download catalog.
Use **Browse...** immediately to its right to select a folder: the launcher scans
that folder (not subfolders) on a worker and adds every regular `.gguf` file to
the list without copying or changing the model files.
**Add Existing Model** uses the Windows GGUF file picker, validates an existing regular
file on a worker, and registers its absolute Unicode path without copying it.
**Download Model** opens a separate, modeless Win32 window: select a catalog entry,
choose an existing folder with **Browse...**, and explicitly press **Download**.
The last chosen download folder is displayed as a suggestion, never used silently.

The built-in catalog is in `src/models/ModelCatalog.cpp`. It includes
[Qwen3.6-35B-A3B Q4_K_M](https://huggingface.co/ggml-org/Qwen3.6-35B-A3B-GGUF/blob/main/Qwen3.6-35B-A3B-Q4_K_M.gguf),
20,419,565,568 bytes, and
[Qwen3.6-27B Q6_K_L](https://huggingface.co/bartowski/Qwen_Qwen3.6-27B-GGUF/blob/main/Qwen_Qwen3.6-27B-Q6_K_L.gguf),
24,291,299,840 bytes. Their HTTPS resolve URLs and SHA-256 values are pinned in the entries.
Adding another model requires a catalog entry, not changes to the main window.
Catalog files are data-only: downloaded files are never executed.

`config.json` is created beside the executable. Schema 2 contains:

- `installed_models`: registrations with stable `id`, `display_name`, absolute `path`,
  `family`, `quant`, and all per-model tuning;
- `active_model_id`: the current ComboBox selection;
- `last_model_download_directory`: convenience value shown before Download;
- `last_model_browse_directory`: most recently scanned model folder;
- existing launcher settings (`llama_command`, `server_host`, `server_port`,
  `workspace_path`, `docker_image`, `docker_enabled`, `auto_start_server`). The old `workspace`
  key is still accepted during migration.

The first-run workspace is `<launcher-directory>\workspace` when that directory
is writable; protected installations fall back to `%LOCALAPPDATA%\AI-Agent-LVK\workspace`.
An existing configured workspace is preserved. **Docker Settings → Change Workspace**
selects and persists another directory, and **Docker Settings → Open Workspace**
opens the persisted directory.

Legacy `profiles` / `selected_profile` and single `model_path` configs migrate
automatically. The active Qwen3-Coder and all stored tuning are preserved, including
8192/16384 contexts: the old automatic promotion to 32768 has been removed.
The original bytes remain in `config.json.pre-models.bak`. Invalid JSON is reported
without overwriting the original. Updates write a temporary config and replace it
atomically; the release package intentionally does not overwrite user configuration.
On a first run without config, the old default Qwen3-Coder path is registered only
if its file exists; otherwise the list starts empty.

A missing file stays registered and is labelled **[Missing]**. Start offers **Locate**,
**Remove from list**, or **Cancel**. Locate changes only the path, preserving tuning.
Remove from List never deletes a GGUF or stops a running server. Open Model Folder
always uses the selected model's saved parent directory. Selecting another model
does not switch a running server: press Restart to apply the selection.

### Download safety and cancellation

The download worker performs folder, write-access and free-space checks (model size
plus at least 5% / 512 MiB reserve). Existing final files require **Use Existing**
(size/hash verification), **Re-download**, or **Cancel**. Existing partial files
require explicit **Restart** or **Cancel**; HTTP Range resume is not yet implemented.

WinHTTP uses native asynchronous requests, HTTPS certificate validation, limited
redirects and no HTTPS-to-HTTP downgrade. The owned worker sends progress through
PostMessage approximately every 150 ms: actual bytes, Content-Length percentage,
average speed and elapsed time. Without Content-Length the progress bar is indeterminate.

Data streams into the chosen `.gguf.part` file. Only after checking GGUF magic,
size, and SHA-256, flushing and closing the file, does a same-directory atomic rename
publish the final GGUF and register it. A failed replacement leaves the old final
file untouched. Filenames come only from validated catalog entries; path traversal,
executable extensions, hard-linked/reparse-point partial files and unapproved
overwrites are rejected.

Cancel sets an atomic flag; the worker closes the asynchronous request and retains
its callback context/read buffer until WinHTTP's final HANDLE_CLOSING notification.
The main window remains responsive. Partial files remain unregistered. Shutdown,
including an updater close request, cancels downloads and drains the owned workers
before destroying their windows. There are no detached download threads.

## Model Settings

Settings, Coding / Creative / Chaos / Custom presets and the context slider are
per installed model. Changes apply on the next Start/Restart. Qwen3.6's initial
profile uses the following editable runtime values:

```json
{
  "context": 8192, "max_context": 262144,
  "parallel": 1, "gpu_layers": 999, "cpu_moe": 27,
  "temperature": 0.3, "top_k": 20, "top_p": 0.95,
  "presence_penalty": 0.0, "repeat_penalty": 1.0, "frequency_penalty": 0.0,
  "batch_size": 512, "ubatch_size": 253,
  "kv_k": "q8_0", "kv_v": "q8_0", "flash_attention": "on",
  "agent_turn_limit": 0,
  "mtp_supported": false, "mtp_model_path": "",
  "spec_type": "none", "spec_draft_n_max": 2,
  "tools": "all", "tools_runtime": "docker:ai-cpp-sandbox"
}
```

8192 is the recommended starting context. The Qwen3.6 catalog profile permits
8192 through 262144 tokens using discrete slider steps; 262144 is the model's
documented context-window ceiling, while actual usable context depends on
available RAM/VRAM and llama.cpp runtime support. Unknown files added manually
remain conservative.
Existing Qwen3.6 registrations receive this capability update without changing
their current context or other per-model settings.
CPU MoE 27 is an initial setting, not a hardware-independent optimum.

Model Settings also stores the per-model **Agent turn limit** (`Off`, `10`, `20`,
`50`, `100`, or `Unlimited`). The launcher passes this to the llama.cpp Web UI
through its supported `--ui-config` setting `agenticMaxTurns`: `Off` omits the
override, numeric values set the limit, and `Unlimited` uses the Web UI's
supported `Infinity` value.

Flash Attention accepts `on`, `auto`, `off`; legacy booleans migrate to on/off.
MTP requires BOTH explicit `mtp_supported=true` and an existing separate
`mtp_model_path`. No filename substring enables it. Controls and launch flags
stay disabled without both conditions; old stored settings are preserved.
When enabled explicitly, the command includes `--model-draft <path>`,
`--spec-type` and `--spec-draft-n-max`. MTP downloading is not part of this version.

For a user-selected example folder, the Qwen3.6 command is:

```text
llama serve -m "G:\AI\models\Qwen3.6-35B-A3B\Qwen3.6-35B-A3B-Q4_K_M.gguf" -c 8192 -np 1 -ngl 999 -ncmoe 27 --temp 0.300000 --top-k 20 --top-p 0.950000 --presence-penalty 0.000000 --repeat-penalty 1.000000 --frequency-penalty 0.000000 --batch-size 512 --ubatch-size 253 -ctk "q8_0" -ctv "q8_0" --flash-attn "on" --tools "all" --tools-runtime "docker:ai-cpp-sandbox" --metrics --host "127.0.0.1" --port 8080
```

The path above is an example, not an installation default. A configured
`llama-server.exe` is invoked directly without the `serve` subcommand.

## Memory monitor

The GUI shows system RAM/VRAM and the RAM/VRAM reported for the launcher-owned `llama serve` process. NVIDIA VRAM is read through the optional driver-provided NVML library; if NVML is unavailable, the launcher reports it as unavailable instead of starting a helper process.
The same background update displays `Speed: N.N tok/s` while llama.cpp is actively generating, using its `/metrics` endpoint and generation-token counters; it displays `idle` when no request is processing and `n/a` while the endpoint is unavailable. The metrics request uses a short timeout and never runs on the GUI thread.

## Docker sandbox

The repository contains `docker\Dockerfile`. Build it from the launcher directory with **Rebuild Sandbox**, or run:

```bat
docker build -t ai-cpp-sandbox docker
```

Docker is used only as the tools sandbox. Inference and CUDA remain native Windows processes.
The launcher creates one owned, temporary Docker container for the running server.
It mounts only the configured host workspace as `/workspace`, sets `/workspace` as
the working directory, and passes llama.cpp `docker-container:<owned-id>` as the
tools runtime. The container uses a read-only root filesystem, tmpfs for `/tmp`
and `/root/.cache`, drops all capabilities, and enables `no-new-privileges`.
It does not use the Docker socket, privileged mode, or broad host-directory mounts.
Persistent project files and deliverables must stay under `/workspace`; `/tmp` is
for disposable intermediate data. Rebuild Sandbox rebuilds only the image and
never deletes or modifies the host workspace.
The image provides the native C/C++ toolchain, SDL2 development libraries,
ncurses, OpenSSL, zlib, Boost, and MinGW-w64. Python, Node.js, .NET, desktop
GUI stacks, Docker socket access, and privileged mode are not added.

## Using the launcher

1. Start `AI-Agent-LVK.exe`.
2. Select a profile.
3. Optionally open **Model Settings** and tune the selected profile.
4. Open **Docker Settings**, enable Docker when tools are needed, then use **Connect / Start Docker**. Use **Rebuild Sandbox** if the `ai-cpp-sandbox` image is missing.
5. Press **Start AI**. With Docker disabled or not connected, the model starts without Docker tools; with a connected sandbox, llama.cpp uses that owned sandbox.
6. Press **Open Web UI** to open `http://127.0.0.1:8080`.
7. Use **Stop** or **Restart** to control only the process launched by this window.

Logs are shown in the window and written to `logs\launcher.log`; llama stdout/stderr is appended there as well.

## Architecture

`ConfigManager`, `DependencyChecker`, `DockerManager`, `LlamaManager`, `ProcessManager`, `PortChecker`, `LogManager`, and the native Model Settings page are Win32/C++ modules. There is no custom HTTP API, command dispatcher, embedded model engine, agent loop, MCP client, or duplicate tool runtime.

## Updates

`LVKUpdater.exe`, `app.update.json`, release packaging, SHA-256 calculation, and `update/manifest.json` generation remain part of the existing release flow. Releases are created from semantic version tags `vX.Y.Z`.
