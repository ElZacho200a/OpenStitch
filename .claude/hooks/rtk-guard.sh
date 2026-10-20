#!/bin/bash
# PreToolUse hook: block RTK anti-patterns in both multi-agent and pairing modes.
# Enforces AGENT_TOOLS.md rules:
#   - Do not read RTK tee files — the compressed summary is the authoritative output.
#   - Do not use rtk proxy.
# No external dependencies (no jq). Portable across Linux and macOS.

input=$(cat)

# Extract JSON string values without jq — matches "key": "value" patterns.
json_val() { echo "$input" | grep -o "\"$1\"[[:space:]]*:[[:space:]]*\"[^\"]*\"" | head -1 | sed 's/.*:[[:space:]]*"//;s/"$//'; }

command=$(json_val command)

blocked=""
reason=""

if echo "$command" | grep -qE '~?/?\.local/share/rtk/tee'; then
  blocked="read RTK tee file"
  reason="RTK compressed output is authoritative. Do not read tee files — diagnose from the summary. Do not try to work around RTK."
fi
# `rtk proxy` (sortie brute, suivie par rtk gain) est autorisé depuis 2026-10-20 : à utiliser
# quand un filtre RTK cache une information nécessaire au diagnostic.

if [[ -n "$blocked" ]]; then
  cat <<EOF
{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"$reason"}}
EOF
fi
