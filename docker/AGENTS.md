# AI-Agent-LVK coding sandbox policy

Persistent workspace is `/workspace`.

All user project files, source files, build outputs and deliverables MUST be created under `/workspace`.
Use `/tmp` only for temporary intermediate files. At the beginning of a coding task inspect `/workspace`,
then create or select a project directory under `/workspace`. Do not place persistent project files in
`/tmp`, `/root`, `/home`, `/opt`, `/usr`, or any other container path. If `AGENT_WORKSPACE` is available,
use it. At the end report the project path inside `/workspace`.
