# AI-Agent-LVK — Codex Instructions

## Project goal

Build a fast native local AI agent runtime in C++ for Windows first, with Linux support possible later.

This project deliberately avoids Python in runtime, Ollama, Electron, Docker, and other heavyweight wrappers. Performance, direct control, and predictable behavior matter more than UI polish.

## Core principles

1. Runtime code must be C++ only.
2. Use `llama.cpp` directly as a library/backend, not through Ollama or a local HTTP wrapper in the final architecture.
3. Keep UI separate from core logic. The first UI is a console and may remain simple for a long time.
4. Internal communication between modules must use native C++ interfaces, not MCP/HTTP/JSON when a direct function call is possible.
5. MCP is an external interoperability boundary:
   - LocalAgent acts as an MCP client for external MCP servers.
   - LocalAgent can also act as an MCP server for external clients.
   - Native tools and MCP tools should appear through one common tool registry/interface.
6. Agents are separate logical modules with different prompts, tools, permissions, memory and contexts. Multiple agents may share the same loaded model weights.
7. Design for speed, low overhead, modularity, and incremental expansion.

## Current state

Current released version: `v0.1.0`.

The project currently contains:

- native C++20 Windows console application;
- CMake build;
- commands: `help`, `version`, `update`, `clear`, `exit`;
- LVK-Updater integration;
- hidden Win32 update bridge so the updater can request graceful application shutdown only after an update is confirmed and downloaded;
- GitHub Actions Windows/MSVC build;
- automatic release ZIP creation;
- automatic SHA256 and package size calculation;
- automatic public updater manifest generation.

Release artifact:

`AI-Agent-LVK-win-x64.zip`

Updater manifest:

`update/manifest.json`

## LVK-Updater behavior

The application uses the separate `LVKUpdater.exe` project.

Important behavior to preserve:

- update checking must not close the main application;
- the main application closes only after the user confirms the update and the package download/hash verification succeeds;
- updater then replaces files and restarts `AI-Agent-LVK.exe`;
- updater runs without an extra console window;
- logs are written under `logs/`;
- `app.update.json` is generated from `app.update.json.in` during CMake configure/build.

The current LVK-Updater integration targets updater version `0.3.1`.

## Versioning and releases

Semantic versions use `X.Y.Z` and release tags use `vX.Y.Z`.

The tagged release workflow builds the EXE, generates the correct local update config, creates the ZIP, calculates SHA256/size, creates a GitHub Release, and updates `update/manifest.json` on `main`.

Do not introduce manual duplicated version maintenance unless absolutely necessary.

## Planned architecture

Target architecture:

```text
LocalAgent Core
├── Runtime
├── Model Runtime
│   ├── llama.cpp backend
│   └── memory planner
│       ├── VRAM
│       ├── RAM
│       ├── KV cache
│       └── MoE placement
├── Agent Runtime
│   ├── Agent
│   ├── AgentManager
│   ├── AgentRouter
│   └── AgentTool
├── Tool Runtime
│   ├── ToolRegistry
│   ├── Permissions
│   └── Native tools
├── MCP Runtime
│   ├── MCP client
│   ├── MCP server
│   ├── stdio transport
│   └── HTTP transport
├── Memory
│   └── SQLite
├── RAG
└── UI
    └── Console initially
```

## Model runtime direction

Use `llama.cpp` as the low-level inference engine.

The application should eventually control model memory placement directly, including:

- GPU layer offload;
- CPU/RAM fallback;
- KV cache placement;
- context size;
- thread count;
- future MoE-aware placement where useful;
- configurable VRAM reserve so Windows/display workloads are not starved.

A future `ModelConfig` / `MemoryPlanner` should support modes such as:

- Auto
- MaxGpu
- Balanced
- MaxContext
- CpuMoE
- Custom

Do not copy Soup-style per-token layer streaming blindly. For inference, persistent CPU/GPU partitioning is usually preferable because repeatedly streaming all weights over PCIe can become the bottleneck.

## Agent runtime direction

Agents should be separate logical instances, for example:

- MainAgent
- CodingAgent
- ResearchAgent
- SystemAgent
- DocumentAgent

Each agent may have its own:

- system prompt;
- tools;
- permissions;
- context;
- memory scope;
- RAG collections;
- sampling settings;
- max step count.

The same loaded `llama_model` weights should be shareable where the llama.cpp API permits, while each agent can maintain its own context/state.

Agents may also be exposed as tools to other agents.

## Tool architecture

Prefer a common native interface such as:

```cpp
class ITool {
public:
    virtual ~ITool() = default;
    virtual ToolResult execute(const ToolArguments& args, ToolContext& context) = 0;
};
```

Possible implementations:

- NativeFileTool
- NativeShellTool
- NativeGitTool
- McpRemoteTool
- AgentTool

The agent should not need to know whether a tool is native, remote over MCP, or another agent.

Initial native tools later may include:

- read_file
- write_file
- list_directory
- search_files
- run_command
- git status/diff operations

## MCP direction

MCP is not the model API. It is the interoperability layer between the agent runtime and external tools/services/clients.

Inside LocalAgent, use direct C++ calls.

At the boundary, provide:

- MCP client support for external MCP servers;
- MCP server support so other applications can call LocalAgent tools/agents;
- common ToolRegistry adapters.

Do not make internal agent-to-agent communication depend on MCP.

## Remote/mobile access direction

A future Android/mobile client should communicate with LocalAgent running on the desktop machine. The large model remains on the PC/GPU.

Prefer a simple dedicated Agent API/WebSocket for the phone rather than forcing the mobile client to use MCP for chat transport. MCP can remain available in parallel for external AI clients/tools.

Security for remote access must include authentication, TLS where appropriate, device authorization, and tool permission boundaries.

## Security rules

Before adding powerful tools, introduce permission controls.

Suggested policy concepts:

- read-only filesystem operations may be auto-approved inside configured workspaces;
- writes should be restricted to configured roots;
- dangerous shell commands require confirmation or are blocked;
- destructive filesystem operations require confirmation;
- process execution needs cwd, timeout and output limits;
- git changes should be inspectable before commit;
- log agent decisions/tool calls for debugging;
- enforce a maximum agent step count to prevent loops.

## Coding style

- C++20 or newer only when justified.
- Prefer standard library facilities (`std::filesystem`, `std::thread`, `std::jthread`, etc.) before adding dependencies.
- Keep dependencies minimal and explicit.
- Prefer RAII and deterministic ownership.
- Avoid giant monolithic source files as new subsystems appear.
- Keep headers focused and interfaces small.
- Windows is the primary platform for now, but isolate Windows-only code where practical.
- Use UTF-8 source files.
- Treat warnings seriously (`/W4` on MSVC).

## Immediate next milestone

The next meaningful milestone is to integrate `llama.cpp` directly into the C++ project and make the console produce a response from a local GGUF model.

Suggested sequence:

1. Add llama.cpp as a pinned dependency/submodule or a reproducible CMake dependency strategy.
2. Introduce `src/model/` with a clean backend interface.
3. Load a GGUF model from a configured path.
4. Detect/use CUDA when available.
5. Implement one simple console `chat` path.
6. Report model load diagnostics: model name, context, GPU offload, RAM/VRAM usage if available.
7. Only after basic inference is stable, add the first Agent Core loop and tools.

Do not jump directly into multi-agent orchestration, RAG, or MCP before basic direct llama.cpp inference is stable.
