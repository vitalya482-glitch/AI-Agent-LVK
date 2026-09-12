# AI-Agent-LVK — Agent Instructions

AI-Agent-LVK is a Windows-first native C++ launcher and manager for an already-built `llama.cpp` server. It is not an inference engine or agent framework.

## Non-negotiable boundaries

- Runtime code is C++20 and Windows-first.
- Do not add Python, Electron, .NET, Ollama, LangChain, or a second inference wrapper.
- Do not reintroduce the deleted custom HTTP API, command dispatcher, embedded llama.cpp model runtime, agent loop, MCP client, or GUI API client.
- `llama serve` and its Web UI provide inference, chat, tools, and model functionality.
- Docker is only the sandbox for llama.cpp tools. Never use it for inference.
- Default network binding must remain `127.0.0.1`; never broaden it to `0.0.0.0` by default.
- The launcher must control only the process handle it created.

## Current architecture

`AI-Agent-LVK.exe` contains `ConfigManager`, `DependencyChecker`, `DockerManager`, `LlamaManager`, `ProcessManager`, `PortChecker`, `LogManager`, a small Win32 GUI, and the preserved LVK-Updater bridge.

Configuration is `config.json` beside the executable. Model profiles supply the llama serve arguments. The default workspace is `G:\AI\workspace`, not the repository.

## Process and safety rules

- Prefer `CreateProcessW` directly with `CREATE_NO_WINDOW`, quoted arguments, and inherited stdout/stderr pipes.
- Validate the llama command, GGUF file, workspace, and port before starting. Validate Docker Engine and the sandbox image only when Docker is enabled and the user is connecting the sandbox.
- Server readiness is based on the configured localhost port, not merely process existence.
- Keep log work off the GUI thread and cap or rotate logs if they grow materially.
- Docker is configured separately from model launch. Start AI must not start Docker or create a sandbox; Docker Settings owns enable/connect/rebuild/disconnect/workspace actions.
- Writes and process execution must stay within the explicit user configuration.

## Build and release

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

The only product executable is `AI-Agent-LVK.exe`. Preserve `app.update.json`, `LVKUpdater.exe` compatibility, the updater close bridge, and the release workflow's package/manifest behavior.

## Release handoff requirements

- When a release task is completed and the build/tests pass, publish the release to GitHub using the repository's existing release workflow and include the verified release artifact.
- Before handing off a local release, clean the local release directory and leave only the working, extracted release files required to run the application. Do not leave ZIP/RAR/7z archives in that directory; the archive may be kept separately only when required by the GitHub release workflow.
- Never report a GitHub release as published until the tag/release and its artifact upload have completed successfully. Never report the local release as clean until the extracted files have been checked.
