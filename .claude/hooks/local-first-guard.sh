#!/usr/bin/env bash
# Signale (ne bloque jamais) les ecarts a la delegation locale : edition directe de code
# ou sous-agent Claude. Journalise dans build/local-first/stats.jsonl.
# Contournement : LOCAL_FIRST_BYPASS=1. Installation/validation (scripts/local, .claude, docs, build) : silencieux.
[ "${LOCAL_FIRST_BYPASS:-0}" = "1" ] && exit 0
input="$(cat)"
tool="$(printf '%s' "$input" | sed -n 's/.*"tool_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1)"
path="$(printf '%s' "$input" | sed -n 's/.*"file_path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1 | tr '\\' '/')"
root="${CLAUDE_PROJECT_DIR:-.}"
log() { mkdir -p "$root/build/local-first" 2>/dev/null; printf '{"kind":"claude-direct","ts":"%s","tool":"%s","path":"%s"}\n' "$(date -u +%FT%TZ)" "$tool" "$1" >> "$root/build/local-first/stats.jsonl" 2>/dev/null; }
say() { printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","additionalContext":"%s"}}' "$1"; }

case "$tool" in
  Agent)
    log "agent"
    say "LOCAL-FIRST: un sous-agent Claude consomme le quota Anthropic. Prefere scripts/local/delegate.ps1 (Ollama/OpenCode) sauf demande explicite de l'utilisateur ou echec local documente."
    ;;
  Edit|Write|MultiEdit)
    case "$path" in
      */apps/*|*/libs/*|*/tests/*)
        log "$path"
        say "LOCAL-FIRST: edition directe de code ($path). Ollama peut-il le faire ? Si oui deleguer via scripts/local/delegate.ps1 (-Level 2, -Scope, -Verify). Sinon justifier l'escalade (-LogClaude)."
        ;;
    esac
    ;;
esac
exit 0
