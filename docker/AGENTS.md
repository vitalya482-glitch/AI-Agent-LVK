# AI-Agent-LVK coding sandbox policy

Persistent workspace is `/workspace`.

All user project files, source files, build outputs and deliverables MUST be created under `/workspace`.
Use `/tmp` only for temporary intermediate files. At the beginning of a coding task inspect `/workspace`,
then create or select a project directory under `/workspace`. Do not place persistent project files in
`/tmp`, `/root`, `/home`, `/opt`, `/usr`, or any other container path. If `AGENT_WORKSPACE` is available,
use it. At the end report the project path inside `/workspace`.

Host OS: Windows x64. The default final target for GUI apps, games, desktop apps,
and console utilities intended for local use is a Windows x64 `.exe` under
`/workspace`. Do not return Linux ELF binaries as final deliverables for Windows
GUI applications; Linux builds are for intermediate validation only.

Use the preinstalled MinGW SDL2 toolchain for Windows builds:

    cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=/opt/ai-agent/mingw64.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build build-win

For SDL2 projects, `build-windows-sdl2 /workspace/project` performs this build
and copies only DLLs referenced by produced PE executables. The helper rejects
Linux ELF outputs, missing Windows `.exe` deliverables, and obvious unconditional
`do/while(true)` or `while(true)` loops without `break` before reporting success.
