# AI-Agent-LVK

Minimal native C++ foundation for a future local AI agent runtime.

The first version is intentionally small:

- one Windows console application;
- no Python, Ollama, Electron or external runtime;
- C++20 + CMake;
- application version output;
- `help`, `version`, `update`, `clear`, `exit` commands;
- integration with the existing **LVK-Updater**;
- hidden Win32 update bridge so LVK-Updater can close the console process only when an update is actually ready to install;
- automatic tagged releases and updater manifest generation through GitHub Actions.

## Current development version

`0.1.0`

## Project layout

```text
AI-Agent-LVK/
├─ .github/workflows/
│  ├─ windows-build.yml
│  └─ release.yml
├─ update/
│  └─ manifest.json
├─ CMakeLists.txt
├─ app.update.json.in
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
build\Release\app.update.json
```

`app.update.json` is generated from `app.update.json.in` using the CMake project version.

A custom version can be supplied explicitly:

```bat
cmake -S . -B build -A x64 -DAI_AGENT_LVK_VERSION=0.1.1
```

## Automatic releases and manifests

A Git tag is the release source of truth. Push a semantic version tag in the form `vX.Y.Z`:

```bat
git tag v0.1.1
git push origin v0.1.1
```

GitHub Actions then automatically:

1. extracts `0.1.1` from the tag;
2. configures CMake with that exact version;
3. builds `AI-Agent-LVK.exe` with MSVC;
4. generates `app.update.json` with the same version;
5. creates `AI-Agent-LVK-win-x64.zip`;
6. calculates the ZIP SHA256 and byte size;
7. creates or updates the matching GitHub Release;
8. rewrites `update/manifest.json` on `main` with the version, release URL, SHA256, size and release date.

This means release metadata does not need to be edited manually.

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
