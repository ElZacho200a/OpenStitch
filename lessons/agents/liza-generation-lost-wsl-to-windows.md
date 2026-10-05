---
title: "LIZA_AGENT_GENERATION lost when a WSL-launched agent runs as a Windows process"
trigger: "When a liza lifecycle command fails with 'agent generation required' in a Windows-hosted agent session"
keywords: [LIZA_AGENT_GENERATION, agent generation required, WSLENV, wsl -d Ubuntu, add-tasks, Git Bash]
date: 2026-09-23
---

## Context

The Liza supervisor runs in WSL (`Ubuntu`) but launches the Claude agent as a
Windows process (claude.exe, Git Bash / PowerShell tools). Agents call the CLI
back through `wsl -d Ubuntu -- bash -lc 'liza -C /mnt/c/... ...'`.

## Failure Mode

WSL→Windows interop forwards only the variables listed in `WSLENV`
(here `WT_SESSION:WT_PROFILE_ID:`). `LIZA_AGENT_GENERATION` never reaches the
Windows process, so it is absent again when the agent re-enters WSL, and every
agent-authenticated command (`add-tasks`, `submit-*`, `unblock-task`…) fails
with `validation: agent generation required`.

## Solution

Workaround used: confirm the registration is this session's own. The
`registered_at` time for `agents.<id>` in state must match the timestamp of
`.liza/agent-prompts/<id>-<YYYYMMDD-HHMMSS>.txt`, and no newer
registration may exist. Then pass that generation inline for the one command:
`wsl -d Ubuntu -- bash -lc 'LIZA_AGENT_GENERATION=<gen> liza -C <root> ...'`.
Never reuse a generation from a newer registration: that is a fencing
violation (see ~/.liza/support-docs/TROUBLESHOOTING.md).

If `liza get agents` shows no generation, read it from the live session
process instead: the agent's `claude.exe` is a child of the supervisor PID
(`liza get agents --json` → `pid`), and its `/proc/<pid>/environ` holds
`LIZA_AGENT_ID` + `LIZA_AGENT_GENERATION`; its start time must match the
newest prompt file. Put the `/proc` scan in a script file and run it with
`MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash /mnt/c/.../script.sh`: `wsl.exe`
mangles `$var` inside `bash -lc '...'`, and Git Bash rewrites `/mnt/...`
arguments to `C:/Program Files/Git/mnt/...` without `MSYS_NO_PATHCONV=1`.

Root fix (human): make the supervisor forward the variable, e.g. export
`WSLENV=$WSLENV:LIZA_AGENT_GENERATION:LIZA_AGENT_ID` before launching agents.

## References

- ~/.liza/support-docs/TROUBLESHOOTING.md#agent-generation-required
- ~/.liza/support-docs/CONFIGURATION.md (LIZA_AGENT_GENERATION)
