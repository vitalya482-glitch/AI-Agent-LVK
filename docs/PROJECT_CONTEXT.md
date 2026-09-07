# AI-Agent-LVK — Project Context

This document summarizes the design decisions and project direction established before active Codex development.

## Why this project exists

The goal is to build a personal local AI system that is fast, native, modular, and under full user control.

The user explicitly does not want to base the runtime on Ollama. The preferred approach is a native C++ application using `llama.cpp` directly as the inference backend.

UI quality is secondary. A console application is acceptable if the system is fast and reliable.

## High-level vision

The final application should become a native local AI agent runtime with:

- one or more local LLMs;
- multiple specialized agents;
- native tool execution;
- memory;
- RAG/document retrieval;
- MCP client support;
- MCP server support;
- remote/mobile access;
- direct control over RAM/VRAM placement;
- strong permission boundaries.

The target should feel like a personal local agent platform rather than a chat wrapper.

## Model backend decision

`llama.cpp` is the preferred low-level model engine.

Reasoning:

- direct C/C++ API;
- GGUF support;
- CUDA support;
- CPU/GPU hybrid inference;
- control over GPU layer offload;
- control over context/KV behavior;
- no need for Ollama or a local HTTP server in the final runtime;
- ability to embed model inference directly in the main process.

The model backend should be abstracted so the Agent Core is not tightly coupled to llama.cpp-specific details.

Conceptually:

```cpp
class IModelEngine {
public:
    virtual ~IModelEngine() = default;
    virtual bool load(const ModelConfig& config) = 0;
    virtual ModelResponse generate(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools) = 0;
};
```

A `LlamaModelEngine` implementation can wrap llama.cpp.

## RAM / VRAM strategy

The application should expose more control than typical local-LLM wrappers.

Important future controls include:

- number/amount of GPU-offloaded layers;
- maximum VRAM budget;
- maximum RAM budget;
- VRAM reserve for the OS/display;
- KV cache placement;
- context size;
- CPU thread count;
- possible MoE-specific CPU placement.

The user was interested in Soup-style low-VRAM ideas. The conclusion was:

- standard llama.cpp CPU/GPU splitting is useful and should be supported;
- KV cache can also be considered separately;
- Soup-style continuous layer streaming is not automatically desirable for token-by-token inference because PCIe weight transfer can become a severe bottleneck;
- a smart persistent RAM/VRAM split is the preferred baseline;
- MoE models may later make CPU/RAM expert placement especially interesting.

A future `MemoryPlanner` should inspect available hardware and choose sensible settings automatically while allowing manual override.

## Multi-agent design

The project is intended to support multiple agents, not just one assistant.

Potential agents:

```text
MainAgent
CodingAgent
ResearchAgent
SystemAgent
DocumentAgent
```

Agents are logical roles, not necessarily separate model copies.

Where practical, one loaded model should be shared while agents use separate contexts/prompts/tools/permissions.

A MainAgent may delegate tasks to other agents. Agents can be represented through the same tool abstraction as other tools.

Example:

```text
User
  ↓
MainAgent
  ├── CodingAgent
  ├── ResearchAgent
  └── SystemAgent
       ↓
shared llama.cpp model runtime
```

## Tool system

Tool execution should be native C++ wherever possible.

Initial useful tools later:

```text
read_file
write_file
list_directory
search_files
run_command
git_status
git_diff
```

The important abstraction is that an agent sees a uniform ToolRegistry.

A tool may internally be:

- native C++;
- remote through MCP;
- another agent.

This allows expansion without changing Agent Core logic.

## MCP decision

MCP is not the model API.

MCP should be treated as the standardized external interoperability layer.

The intended architecture has both sides:

### LocalAgent as MCP client

It can consume tools from external MCP servers.

Examples may eventually include GitHub, databases, browsers, home automation, CAN tooling, etc.

### LocalAgent as MCP server

External applications can call capabilities exposed by LocalAgent, for example:

```text
ask_coding_agent
search_local_files
analyze_document
search_memory
```

### Internal rule

Do not route ordinary internal calls through MCP just for uniformity.

Inside the process, prefer direct native C++ calls for performance.

MCP adapters sit at the boundary around the same ToolRegistry.

## Mobile/remote vision

The user wants to eventually write a phone client and communicate with the home/local agent remotely.

The intended design is:

```text
Android app
    ↓
Agent API / WebSocket
    ↓
LocalAgent on desktop
    ↓
Agents / tools / memory
    ↓
llama.cpp + GPU
```

The phone does not need to run the large model. It acts as a client.

For the mobile chat transport, a simple dedicated API is preferred over forcing MCP into a role it is not primarily intended for.

MCP can run in parallel for external agent/tool interoperability.

## Security direction

Powerful local tools introduce real risk, so permission controls are mandatory before broad filesystem/shell/system access is added.

Important concepts:

- configurable workspace roots;
- path validation;
- confirmation requirements;
- blocked dangerous actions;
- command timeouts;
- output size limits;
- audit/tool-call logs;
- max agent iteration/step limits;
- inspectable diffs before destructive changes/commits.

## Current implementation status

The project began intentionally small.

Current functionality:

- native C++20 Windows console;
- CMake build;
- simple console loop;
- commands `help`, `version`, `update`, `clear`, `exit`;
- current release `v0.1.0`;
- GitHub Actions build;
- LVK-Updater integration;
- automatic release package generation;
- automatic updater manifest generation.

The first public release was successfully produced end-to-end.

Release asset:

```text
AI-Agent-LVK-win-x64.zip
```

The release process successfully generated package metadata including SHA256 and size and updated `update/manifest.json`.

## LVK-Updater integration details

The project reuses the user's existing LVK-Updater project rather than reimplementing updating.

The updater behavior was intentionally preserved:

1. AI-Agent-LVK launches LVK-Updater in check mode.
2. AI-Agent-LVK remains open while the update is checked/downloaded.
3. If there is no update, the main application keeps running.
4. If an update exists, the updater asks for confirmation.
5. Package is downloaded and verified.
6. Only then does LVK-Updater send a graceful close request.
7. A hidden Win32 window in AI-Agent-LVK receives `WM_CLOSE` so a console application can be closed cleanly by the updater.
8. Updater replaces files and restarts `AI-Agent-LVK.exe`.

This update behavior should not be broken when model inference threads and background services are added later. Eventually the shutdown path should be upgraded from immediate process termination to coordinated subsystem shutdown.

## Release automation

The release mechanism is intended to minimize manual metadata edits.

Important goals:

- semantic versioning;
- release builds use the exact release version;
- generated `app.update.json` uses the same version;
- ZIP is built automatically;
- SHA256 is calculated automatically;
- byte size is calculated automatically;
- GitHub Release is created automatically;
- public updater manifest is updated automatically.

Do not reintroduce independent hardcoded version numbers in several files.

## Development philosophy

Build vertically in small, testable stages.

Preferred sequence:

### v0.1 — foundation

Done:

- native console;
- build pipeline;
- updater;
- release automation.

### Next — first local inference

- integrate llama.cpp directly;
- configure CUDA-capable build path;
- load a GGUF model;
- generate one answer from console input;
- expose diagnostics.

### Then — Agent Core

- message/context abstraction;
- tool definitions;
- structured tool call protocol;
- think → tool → observation → continue loop;
- max steps and logging.

### Then — basic native tools

- filesystem;
- shell/process;
- git.

### Then — persistent memory / RAG

- SQLite;
- embeddings/retrieval architecture;
- document indexing.

### Then — MCP

- MCP client;
- MCP server;
- tool adapters;
- transports.

### Then — multi-agent routing

- MainAgent;
- specialist agents;
- shared model runtime;
- separate contexts/permissions.

### Later — remote/mobile client

- authenticated API;
- WebSocket streaming;
- Android client;
- optional voice pipeline.

## User preferences that affect engineering decisions

- Strictly C++ runtime.
- Speed over interface beauty.
- Console-first is acceptable.
- Avoid Ollama.
- Avoid unnecessary wrapper layers.
- Prefer direct low-level control of llama.cpp and hardware utilization.
- Architecture should be expandable, but early versions should remain small and understandable.
- Changes should remain compatible with the existing LVK-Updater/release workflow.

## What Codex should do next

Before adding large abstractions, inspect the current repository and keep changes incremental.

The recommended next coding task is:

> Integrate a pinned/reproducible llama.cpp dependency into the CMake C++ project and create a minimal `ModelEngine` that can load a user-selected GGUF model and perform one interactive console completion/chat turn, with CUDA support when available.

Keep this milestone isolated from tools, MCP, RAG, memory and multi-agent routing so model loading/inference can be validated first.
