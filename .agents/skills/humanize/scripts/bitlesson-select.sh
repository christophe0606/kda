#!/usr/bin/env bash

set -euo pipefail

# ========================================
# Source Shared Libraries
# ========================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
source "$SCRIPT_DIR/lib/config-loader.sh"
source "$SCRIPT_DIR/lib/model-router.sh"
source "$SCRIPT_DIR/lib/codex-cli.sh"
source "$SCRIPT_DIR/../hooks/lib/project-root.sh"

PLUGIN_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(resolve_project_root)" || {
    echo "Error: Cannot determine project root." >&2
    echo "  Set CLAUDE_PROJECT_DIR or run inside a git repository." >&2
    exit 1
}
MERGED_CONFIG="$(load_merged_config "$PLUGIN_ROOT" "$PROJECT_ROOT")"
BITLESSON_MODEL="$(get_config_value "$MERGED_CONFIG" "bitlesson_model")"
BITLESSON_MODEL="${BITLESSON_MODEL:-haiku}"
CODEX_FALLBACK_MODEL="$(get_config_value "$MERGED_CONFIG" "codex_model")"
PROVIDER_MODE="${HUMANIZE_PROVIDER_MODE:-$(get_config_value "$MERGED_CONFIG" "provider_mode")}"
PROVIDER_MODE="${PROVIDER_MODE:-auto}"

# Source portable timeout wrapper
source "$SCRIPT_DIR/portable-timeout.sh"

# Source shared loop library (kept for consistency with ask-codex.sh)
HOOKS_LIB_DIR="$(cd "$SCRIPT_DIR/../hooks/lib" && pwd)"
source "$HOOKS_LIB_DIR/loop-common.sh"
CODEX_FALLBACK_MODEL="${CODEX_FALLBACK_MODEL:-$DEFAULT_CODEX_MODEL}"

usage() {
    cat <<'USAGE_EOF' >&2
Usage:
  bitlesson-select.sh --task <string> --paths <comma-separated> --bitlesson-file <path>

Output (exactly):
  LESSON_IDS: <comma-separated IDs or NONE>
  RATIONALE: <one concise sentence>
USAGE_EOF
}

TASK=""
PATHS=""
BITLESSON_FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --task)
            TASK="${2:-}"
            shift 2
            ;;
        --paths)
            PATHS="${2:-}"
            shift 2
            ;;
        --bitlesson-file)
            BITLESSON_FILE="${2:-}"
            shift 2
            ;;
        *)
            echo "Error: Unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ -z "$TASK" ]]; then
    echo "Error: --task is required and must be non-empty" >&2
    usage
    exit 1
fi

if [[ -z "$PATHS" ]]; then
    echo "Error: --paths is required and must be non-empty" >&2
    usage
    exit 1
fi

if [[ -z "$BITLESSON_FILE" ]]; then
    echo "Error: --bitlesson-file is required" >&2
    usage
    exit 1
fi

if [[ ! -f "$BITLESSON_FILE" ]]; then
    echo "Error: BitLesson file not found: $BITLESSON_FILE" >&2
    exit 1
fi

BITLESSON_CONTENT="$(cat "$BITLESSON_FILE")"
if [[ -z "$(printf '%s' "$BITLESSON_CONTENT" | tr -d ' \t\n\r')" ]]; then
    echo "Error: BitLesson file is empty (whitespace only): $BITLESSON_FILE" >&2
    exit 1
fi

# Only recorded entries count. Ignore Markdown examples and HTML comments;
# remember the actual IDs so the model cannot select invented lessons.
RECORDED_LESSON_IDS="$(printf '%s\n' "$BITLESSON_CONTENT" | awk '
    {
        sub(/\r$/, "")
        line = $0
        if (fence != "") {
            close_line = line
            sub(/^[ ]*/, "", close_line)
            run = close_line
            sub(/[^`~].*$/, "", run)
            tail = substr(close_line, length(run) + 1)
            if (run ~ ("^" fence "+$") && length(run) >= fence_length && tail ~ /^[ \t]*$/)
                fence = ""
            next
        }
        # Remove comments, including comments spanning multiple lines.
        visible = ""
        while (length(line)) {
            if (comment) {
                pos = index(line, "-->")
                if (!pos) { line = ""; break }
                line = substr(line, pos + 3)
                comment = 0
            } else {
                pos = index(line, "<!--")
                if (!pos) { visible = visible line; break }
                visible = visible substr(line, 1, pos - 1)
                line = substr(line, pos + 4)
                comment = 1
            }
        }
        line = visible
        if (line ~ /^[ ]*(```+|~~~+)/) {
            sub(/^[ ]*/, "", line)
            fence = substr(line, 1, 1)
            sub(/[^`~].*$/, "", line)
            fence_length = length(line)
            next
        }
        if (line ~ /^[[:space:]]*##[[:space:]]+Lesson:/) {
            in_lesson = 1
            next
        }
        if (line ~ /^[[:space:]]*#{1,2}[[:space:]]/) in_lesson = 0
        if (in_lesson && line ~ /^[[:space:]]*Lesson ID:[[:space:]]*/) {
            sub(/^[[:space:]]*Lesson ID:[[:space:]]*/, "", line)
            sub(/[[:space:]]*$/, "", line)
            if (line ~ /^[[:alnum:]][[:alnum:]_.-]*$/ && line != "NONE") print line
        }
    }
')"

if [[ -z "$RECORDED_LESSON_IDS" ]]; then
    printf 'LESSON_IDS: NONE\n'
    printf 'RATIONALE: The BitLesson file has no recorded lessons yet.\n'
    exit 0
fi

# ========================================
# Determine Provider from BITLESSON_MODEL
# ========================================

BITLESSON_PROVIDER="$(detect_provider "$BITLESSON_MODEL")"

if [[ "$PROVIDER_MODE" == "codex-only" ]] && [[ "$BITLESSON_PROVIDER" == "claude" ]]; then
    BITLESSON_MODEL="$CODEX_FALLBACK_MODEL"
    BITLESSON_PROVIDER="codex"
fi

# ========================================
# Conditional Dependency Check (with fallback)
# ========================================

if ! check_provider_dependency "$BITLESSON_PROVIDER" 2>/dev/null; then
    # Fall back to codex provider when the configured provider binary is missing
    BITLESSON_MODEL="$DEFAULT_CODEX_MODEL"
    BITLESSON_PROVIDER="codex"
    check_provider_dependency "$BITLESSON_PROVIDER"
fi

# ========================================
# Detect Project Root (for -C)
# ========================================

BITLESSON_DIR="$(cd "$(dirname "$BITLESSON_FILE")" && pwd -P)"
if git -C "$BITLESSON_DIR" rev-parse --show-toplevel &>/dev/null; then
    CODEX_PROJECT_ROOT="$(git -C "$BITLESSON_DIR" rev-parse --show-toplevel)"
else
    CODEX_PROJECT_ROOT="$BITLESSON_DIR"
fi

# ========================================
# Build Selector Prompt
# ========================================

PROMPT="$(cat <<EOF
# BitLesson Selector

You select which lessons from the configured BitLesson file (normally \`.humanize/bitlesson.md\`) must be applied for a given sub-task.

## Input

Sub-task description:
$TASK

Related file paths (comma-separated):
$PATHS

BitLesson file content:
<<<BEGIN_BITLESSON_MD
$BITLESSON_CONTENT
<<<END_BITLESSON_MD

## Decision Rules

1. Match only lessons that are directly relevant to the sub-task scope and failure mode.
2. Prefer precision over recall: do not include weakly related lessons.
3. If nothing is relevant, return \`NONE\`.
4. Use only the information in this prompt. Do not use tools, shell commands, browser access, MCP servers, or repository inspection.

## Output Format (Stable)

Return exactly two lines (no code blocks, no extra whitespace, no additional sections):

LESSON_IDS: <comma-separated lesson IDs or NONE>
RATIONALE: <one concise sentence>
EOF
)"

# ========================================
# Run Selector (Codex or Claude)
# ========================================

SELECTOR_TIMEOUT=120

run_selector() {
    local provider="$1"
    local model="$2"

    if [[ "$provider" == "codex" ]]; then
        humanize_codex_exec_args read-only
        local codex_exec_args=("${CODEX_AUTOMATION_ARGS[@]}")
        # Probe for --skip-git-repo-check and --ephemeral support
        if codex exec --help 2>&1 | grep -q -- '--skip-git-repo-check'; then
            codex_exec_args+=("--skip-git-repo-check")
        fi
        if codex exec --help 2>&1 | grep -q -- '--ephemeral'; then
            codex_exec_args+=("--ephemeral")
        fi
        codex_exec_args+=(
            "-m" "$model"
            "-c" "model_reasoning_effort=low"
            "-C" "$CODEX_PROJECT_ROOT"
        )
        # Codex diagnostics may echo the prompt, including its output template.
        # Parse only the final assistant message, never the terminal transcript.
        local message_file
        message_file="$(mktemp)" || return 1
        trap 'rm -f -- "$message_file"' EXIT
        local status=0
        printf '%s' "$PROMPT" | run_with_timeout "$SELECTOR_TIMEOUT" codex exec "${codex_exec_args[@]}" \
            --output-last-message "$message_file" - >&2 || status=$?
        if [[ $status -eq 0 ]]; then
            cat "$message_file" || status=$?
        fi
        rm -f -- "$message_file"
        trap - EXIT
        return "$status"
    fi

    if [[ "$provider" == "claude" ]]; then
        printf '%s' "$PROMPT" | run_with_timeout "$SELECTOR_TIMEOUT" claude --print --model "$model" -
        return $?
    fi

    echo "Error: Unsupported BitLesson provider '$provider'" >&2
    return 1
}

CODEX_EXIT_CODE=0
RAW_OUTPUT="$(run_selector "$BITLESSON_PROVIDER" "$BITLESSON_MODEL")" || CODEX_EXIT_CODE=$?

if [[ $CODEX_EXIT_CODE -eq 124 ]]; then
    echo "Error: BitLesson selector timed out after ${SELECTOR_TIMEOUT} seconds" >&2
    exit 124
fi

if [[ $CODEX_EXIT_CODE -ne 0 ]]; then
    echo "Error: BitLesson selector failed (exit code $CODEX_EXIT_CODE)" >&2
    printf '%s\n' "$RAW_OUTPUT" >&2
    exit "$CODEX_EXIT_CODE"
fi

# ========================================
# Enforce Stable Output Format
# ========================================

invalid_output() {
    echo "Error: Invalid selector response: $1" >&2
    printf '%s\n' "$RAW_OUTPUT" >&2
    exit 1
}

# Accept CRLF but require the two specified lines, in order, exactly once.
RAW_OUTPUT="${RAW_OUTPUT//$'\r'/}"
mapfile -t RESPONSE_LINES <<< "$RAW_OUTPUT"
if [[ ${#RESPONSE_LINES[@]} -ne 2 || ${RESPONSE_LINES[0]} != 'LESSON_IDS: '* || ${RESPONSE_LINES[1]} != 'RATIONALE: '* ]]; then
    invalid_output "expected exactly LESSON_IDS and RATIONALE lines"
fi
LESSON_IDS_VALUE="${RESPONSE_LINES[0]#LESSON_IDS: }"
RATIONALE_VALUE="${RESPONSE_LINES[1]#RATIONALE: }"
if [[ -z "${RATIONALE_VALUE//[[:space:]]/}" || "$RATIONALE_VALUE" == *'<one concise sentence>'* ]]; then
    invalid_output "missing rationale or literal template placeholder"
fi

if [[ "$LESSON_IDS_VALUE" != NONE ]]; then
    ID_LIST_PATTERN='^[[:alnum:]][[:alnum:]_.-]*(,[[:space:]]*[[:alnum:]][[:alnum:]_.-]*)*$'
    [[ "$LESSON_IDS_VALUE" =~ $ID_LIST_PATTERN ]] || invalid_output "expected recorded lesson IDs or NONE"
    IFS=',' read -r -a SELECTED_IDS <<< "$LESSON_IDS_VALUE"
    for lesson_id in "${SELECTED_IDS[@]}"; do
        lesson_id="${lesson_id//[[:space:]]/}"
        if ! grep -Fxq -- "$lesson_id" <<< "$RECORDED_LESSON_IDS"; then
            invalid_output "unknown lesson ID: $lesson_id"
        fi
    done
fi

printf 'LESSON_IDS: %s\n' "$LESSON_IDS_VALUE"
printf 'RATIONALE: %s\n' "$RATIONALE_VALUE"
