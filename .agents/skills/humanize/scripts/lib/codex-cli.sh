#!/usr/bin/env bash
# Shared arguments for non-interactive, nested Codex calls. Do not inherit a
# user's permissive approval policy, and never run the outer session's hooks.
humanize_codex_exec_args() {
    local sandbox="${1:-workspace-write}"
    CODEX_AUTOMATION_ARGS=(--disable hooks -c 'approval_policy="never"' --sandbox "$sandbox")
    if [[ "${HUMANIZE_CODEX_BYPASS_SANDBOX:-}" == true || "${HUMANIZE_CODEX_BYPASS_SANDBOX:-}" == 1 ]]; then
        CODEX_AUTOMATION_ARGS=(--disable hooks --dangerously-bypass-approvals-and-sandbox)
    fi
}
