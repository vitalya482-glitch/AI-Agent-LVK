# AI-Agent-LVK

Minimal native C++ foundation for a future local AI agent runtime.

The first version is intentionally small:

- one Windows console application;
- no Python, Ollama, Electron or external runtime;
- C++20 + CMake;
- application version output;
- `help`, `version`, `update`, `clear`, `exit` commands;
- integration with the existing **LVK-Updater**;
- hidden Win32 update bridge so LVK-Updater can close the console process only when an update is actually ready to install.

## Current version

`0.1.0`

## Project layout

```text
AI-Agent-LVK/
├─ CMakeLists.txt
├─ app.update.json
├─ README.md
└─ src/
   ├─ main.cpp
   └─ update/
      ├─ UpdateCloseBridge.h
      ├─ UpdateCloseBridge.cpp
      ├─ UpdateManager.h
      └─ UpdateManager.cpp
```

The structure will later grow with separate modules for model runtime, agents, tools, MCP, memory and RAG without changing the console/UI layer.

## Build

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 with Desktop development with C++
- CMake 3.20+

From a Developer Command Prompt:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output:

```text
build\Release\AI-Agent-LVK.exe
```

CMake automatically copies `app.update.json` next to the executable.

## LVK-Updater

Place the current `LVKUpdater.exe` next to `AI-Agent-LVK.exe`:

```text
AI-Agent-LVK.exe
LVKUpdater.exe
app.update.json
```

The current integration targets LVK-Updater `0.3.1` and starts it in manifest check mode.

Command inside AI-Agent-LVK:

```text
update
```

Behavior:

1. AI-Agent-LVK launches LVK-Updater silently.
2. The console application stays open while the updater checks the manifest.
3. If there is no update, AI-Agent-LVK continues running.
4. If an update exists, LVK-Updater asks for confirmation and downloads/verifies it.
5. Only after successful download and verification does LVK-Updater send `WM_CLOSE` to the hidden update bridge.
6. AI-Agent-LVK exits, the updater replaces files, and starts `AI-Agent-LVK.exe` again.

Updater logs are written to:

```text
logs\updater.log
```

## Next milestones

The next layers are expected to be added independently:

```text
src/model/     llama.cpp backend and model memory planner
src/agent/     agent runtime and routing
src/tools/     native tools
src/mcp/       MCP client/server
src/memory/    persistent memory
src/rag/       local retrieval/indexing
```
