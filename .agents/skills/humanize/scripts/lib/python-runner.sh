#!/usr/bin/env bash
# Prefer uv, then an interpreter from PATH. Do not retry a failed invocation:
# scripts may have side effects and their exit status belongs to the caller.

humanize_python_available() {
    command -v uv >/dev/null 2>&1 ||
        command -v python3 >/dev/null 2>&1 ||
        command -v python >/dev/null 2>&1
}

humanize_run_python() {
    if command -v uv >/dev/null 2>&1; then
        case "${1:-}" in
            ""|-*) uv run python "$@" ;;
            *) uv run "$@" ;;
        esac
    elif command -v python3 >/dev/null 2>&1; then
        python3 "$@"
    elif command -v python >/dev/null 2>&1; then
        python "$@"
    else
        printf 'Error: Humanize requires uv or Python on PATH.\n' >&2
        return 127
    fi
}
