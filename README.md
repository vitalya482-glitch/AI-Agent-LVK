# AI-Agent-LVK

Native C++ foundation for a local AI agent runtime.

The project deliberately keeps the runtime small and low-level:

- C++20 + CMake;
- no Python runtime, Ollama, Electron or Docker;
- console core process;
- local HTTP API;
- separate minimal Win32 GUI client;
- LVK-Updater integration;
- Windows first, while keeping the network/API layer portable for a later Linux move.

## Current development version

`0.1.2`

## Architecture

```text
Console --------------------+
                            |
Win32 GUI -- HTTP ----------+--> CommandDispatcher --> Core
                            |
Future Android/Linux client +
```

The GUI never drives the console through stdin/stdout. Console and HTTP are separate front ends over the same core command dispatcher.

The model/agent layers are not implemented yet. The reserved direction is:

```text
HTTP / Console
      |
CommandDispatcher / Agent API
      |
AgentManager
  |-- MainAgent
  |-- CodingAgent
  |-- ResearchAgent
  `-- ...
      |
ModelManager
      |
llama.cpp
```

Multiple logical agents are expected to share one loaded model's weights where practical while keeping separate prompts, contexts, tools, permissions and memory scopes.

## Build

Windows requirements:

- Windows 10/11 x64
- Visual Studio Build Tools / Visual Studio with Desktop development with C++
- CMake 3.20+

From a Developer Command Prompt:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Output:

```text
build\Release\AI-Agent-LVK.exe
build\Release\AI-Agent-LVK-GUI.exe
build\Release\app.update.json
```

`AI-Agent-LVK.exe` is the core/console/API process.

`AI-Agent-LVK-GUI.exe` is intentionally a very simple native Win32 HTTP client.

## GUI v0.1.2

The GUI now:

- checks the HTTP API automatically every 2 seconds;
- shows connected/disconnected state;
- has `Start Core` to launch `AI-Agent-LVK.exe` from the same directory;
- has `Restart Core` to stop the current Windows core instance and start it again;
- sends commands such as `ping`, `status`, `version` and `help` through HTTP only.

The Windows core control buttons are platform-specific UI helpers. Future Linux and Android clients continue to use the same HTTP API instead of depending on Win32 behavior.

## Console commands

```text
help
version
status
ping
update
clear
exit
```

`clear` and `exit` are console-only UI commands. Core commands are executed through `CommandDispatcher`, which is also used by the HTTP API.

## HTTP API v1

The current server binds only to loopback for safety:

```text
http://127.0.0.1:7842
```

Endpoints:

```text
GET  /api/v1/status
GET  /api/v1/version
POST /api/v1/command
POST /api/v1/chat
```

Example command request:

```json
{
  "command": "status"
}
```

`POST /api/v1/chat` is reserved now and returns `model_not_loaded` until llama.cpp is integrated.

The HTTP parser is intentionally minimal and is not intended to be a general web server.

## Cross-platform direction

The HTTP server is separated from socket operations:

```text
src/net/HttpServer.*
src/net/PlatformSocket.h
src/net/platform/WindowsSocket.cpp
src/net/platform/PosixSocket.cpp
```

Windows uses Winsock2. POSIX socket support is already present for the future Linux core.

The current Win32 GUI itself is platform-specific by design. A Linux or Android client should use the same HTTP API rather than share Win32 UI code.

Remote/mobile access is not enabled yet. Before binding beyond `127.0.0.1`, authentication, permissions and transport security must be designed.

## LVK-Updater

Place the current `LVKUpdater.exe` next to the core executable:

```text
AI-Agent-LVK.exe
AI-Agent-LVK-GUI.exe
LVKUpdater.exe
app.update.json
```

The update command launches the existing updater. On Windows the hidden update bridge behavior remains unchanged: the core is closed only after an update is confirmed, downloaded and verified.

## Automatic releases

A semantic version tag such as:

```bat
git tag v0.1.2
git push origin v0.1.2
```

triggers the Windows release workflow. The release ZIP includes both `AI-Agent-LVK.exe` and `AI-Agent-LVK-GUI.exe`, plus `app.update.json`.

## Next milestone

After the API/GUI foundation is stable:

1. integrate pinned/reproducible `llama.cpp`;
2. add a clean `ModelEngine` interface;
3. load a GGUF model;
4. use CUDA where available;
5. connect `/api/v1/chat` and console chat to the same model runtime;
6. only then begin the agent loop/tools layer.
