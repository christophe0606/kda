#!/usr/bin/env bash
#
# Template loading functions for RLCR loop hooks
#
# This library provides functions to load and render prompt templates.
#
# Template Variable Syntax
# ========================
# Templates use {{VARIABLE_NAME}} syntax for placeholders.
# - Variable names: uppercase letters, numbers, underscores only
# - Example: {{PLAN_FILE}}, {{CURRENT_ROUND}}, {{GOAL_TRACKER_FILE}}
# - Single-pass substitution: {{VAR}} in a value will NOT be expanded
# - Missing variables: placeholder is kept as-is (e.g., {{UNDEFINED}})
#
# Available functions:
# - get_template_dir: Get path to template directory
# - load_template: Load a template file by name
# - render_template: Replace {{VAR}} placeholders with values
# - load_and_render: Load and render in one call
# - load_and_render_safe: Same as above but with fallback for missing templates
# - validate_template_dir: Check if template directory is valid
#

# Get the template directory path
# This is relative to the hooks/lib directory (goes up 2 levels to plugin root)
get_template_dir() {
    local script_dir="$1"
    local plugin_root
    plugin_root="$(cd "$script_dir/../.." && pwd)"
    echo "$plugin_root/prompt-template"
}

# Load a template file and output its contents
# Usage: load_template "$TEMPLATE_DIR" "codex/full-alignment-review.md"
# Returns empty string if file not found
load_template() {
    local template_dir="$1"
    local template_name="$2"
    local template_path="$template_dir/$template_name"

    if [[ "${HUMANIZE_PROVIDER_MODE:-}" == codex-only ]]; then
        local override="$template_dir/codex-host/$template_name"
        if [[ -f "$override" ]]; then
            cat "$override"
            return
        fi
    fi

    if [[ -f "$template_path" ]]; then
        if [[ "${HUMANIZE_PROVIDER_MODE:-}" == codex-only ]]; then
            # Adapt only template-authored text, before inserting user summaries
            # and review results. Never rewrite quoted code or user content.
            codex_host_prompt "$(cat "$template_path")"
        else
            cat "$template_path"
        fi
    else
        echo "Warning: Template not found: $template_path" >&2
    fi
}

codex_host_prompt() {
    printf '%s\n' "$1" | sed \
        -e 's/Claude Code/the implementing Codex session/g' \
        -e 's/Claude/the implementing Codex agent/g' \
        -e 's/CLAUDE.md/AGENTS.md/g' \
        -e 's/AskUserQuestion/the available user-question tool/g' \
        -e 's/TodoWrite/update_plan/g' \
        -e 's/TaskUpdate/update_plan/g' \
        -e 's@/humanize:ask-codex@$ask-codex@g' \
        -e 's@/humanize:gen-plan@$humanize-gen-plan@g' \
        -e 's@/humanize:start-rlcr-loop@$humanize-rlcr@g' \
        -e '/code-simplifier/c\Review your changes for clarity using the available Codex tools; no additional plugin is required.'
}

# Render a template with multiple variable substitutions (single-pass)
# Usage: render_template "$template_content" "VAR1=value1" "VAR2=value2" ...
# Variables should be passed as VAR=value pairs
#
# IMPORTANT: This uses a single-pass approach to prevent placeholder injection.
# If a variable value contains {{OTHER_VAR}}, it will NOT be replaced.
# This prevents content like REVIEW_CONTENT from having its {{...}} patterns
# accidentally substituted, which could corrupt prompts.
render_template() {
    local content="$1"
    shift

    # Build environment variables for all substitutions
    # Using TMPL_VAR_ prefix to avoid conflicts
    local -a env_vars=()
    for var_assignment in "$@"; do
        local var_name="${var_assignment%%=*}"
        local var_value="${var_assignment#*=}"
        # The reviewer runs as a native Windows process. Shell paths such as
        # /c/... or /tmp/... are not usable by its PowerShell file tools.
        # Convert path placeholders only, never summary/review text.
        if [[ "${HUMANIZE_PROVIDER_MODE:-}" == codex-only && "$var_value" == /* ]] && command -v cygpath >/dev/null 2>&1; then
            case "$var_name" in
                *_FILE|*_PATH|*_DIR|PROJECT_ROOT)
                    var_value=$(cygpath -m "$var_value")
                    ;;
            esac
        fi
        env_vars+=("TMPL_VAR_${var_name}=${var_value}")
    done

    # Single-pass replacement using awk
    # Scans for {{VAR}} patterns and replaces them with values from environment
    # Replaced content goes directly to output without re-scanning
    local awk_exit=0
    content=$(env "${env_vars[@]}" awk '
    BEGIN {
        # Build lookup table from environment variables with TMPL_VAR_ prefix
        for (name in ENVIRON) {
            if (substr(name, 1, 9) == "TMPL_VAR_") {
                var_name = substr(name, 10)  # Remove prefix
                vars[var_name] = ENVIRON[name]
            }
        }
    }
    {
        line = $0
        result = ""

        # Process line character by character, looking for {{ patterns
        while (length(line) > 0) {
            # Find next {{
            open_idx = index(line, "{{")
            if (open_idx == 0) {
                # No more placeholders, append rest of line
                result = result line
                break
            }

            # Append everything before {{
            result = result substr(line, 1, open_idx - 1)
            line = substr(line, open_idx)  # line now starts with {{

            # Find closing }}
            close_idx = index(substr(line, 3), "}}")
            if (close_idx == 0) {
                # No closing }}, treat {{ as literal
                result = result substr(line, 1, 2)
                line = substr(line, 3)
                continue
            }

            # Extract variable name (between {{ and }})
            var_name = substr(line, 3, close_idx - 1)
            placeholder = "{{" var_name "}}"

            # Look up in our variables table
            if (var_name in vars) {
                # Replace with value (value goes to output, not re-scanned)
                result = result vars[var_name]
            } else {
                # Keep original placeholder if not found
                result = result placeholder
            }

            # Move past the placeholder
            line = substr(line, length(placeholder) + 1)
        }

        print result
    }' <<< "$content") || awk_exit=$?

    if [[ $awk_exit -ne 0 ]]; then
        echo "Error: Template rendering failed (awk exit code: $awk_exit)" >&2
        return 1
    fi

    echo "$content"
}

# Load and render a template in one step
# Usage: load_and_render "$TEMPLATE_DIR" "block/git-not-clean.md" "GIT_ISSUES=uncommitted changes"
load_and_render() {
    local template_dir="$1"
    local template_name="$2"
    shift 2

    local content
    content=$(load_template "$template_dir" "$template_name")

    if [[ -n "$content" ]]; then
        render_template "$content" "$@"
    fi
}

# Append content from another template file
# Usage: append_template "$base_content" "$TEMPLATE_DIR" "claude/post-alignment.md"
# Only appends if the template exists and is non-empty.
append_template() {
    local base_content="$1"
    local template_dir="$2"
    local template_name="$3"

    local additional_content
    additional_content=$(load_template "$template_dir" "$template_name" 2>/dev/null) || true

    echo "$base_content"
    if [[ -n "$additional_content" ]]; then
        echo "$additional_content"
    fi
}

# ========================================
# Safe versions with fallback messages
# ========================================

# Emit a fallback message, optionally rendering template variables.
_emit_fallback() {
    local fallback_msg="$1"
    shift
    if [[ "${HUMANIZE_PROVIDER_MODE:-}" == codex-only ]]; then
        fallback_msg=$(codex_host_prompt "$fallback_msg")
    fi
    if [[ $# -gt 0 ]]; then
        render_template "$fallback_msg" "$@"
    else
        echo "$fallback_msg"
    fi
}

# Load and render with a fallback message if template fails
# Usage: load_and_render_safe "$TEMPLATE_DIR" "block/message.md" "fallback message" "VAR=value" ...
# Returns fallback message if template is missing or empty
load_and_render_safe() {
    local template_dir="$1"
    local template_name="$2"
    local fallback_msg="$3"
    shift 3

    local content
    content=$(load_template "$template_dir" "$template_name" 2>/dev/null) || true

    if [[ -z "$content" ]]; then
        _emit_fallback "$fallback_msg" "$@"
        return
    fi

    local result
    result=$(render_template "$content" "$@") || true

    if [[ -z "$result" ]]; then
        _emit_fallback "$fallback_msg" "$@"
        return
    fi

    echo "$result"
}

# Validate that TEMPLATE_DIR exists and contains templates
# Usage: validate_template_dir "$TEMPLATE_DIR"
# Returns 0 if valid, 1 if not
validate_template_dir() {
    local template_dir="$1"

    if [[ ! -d "$template_dir" ]]; then
        echo "ERROR: Template directory not found: $template_dir" >&2
        return 1
    fi

    local required_subdirs=("block" "codex" "claude" "plan")
    local missing=()
    local subdir
    for subdir in "${required_subdirs[@]}"; do
        if [[ ! -d "$template_dir/$subdir" ]]; then
            missing+=("$subdir")
        fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "ERROR: Template directory missing subdirectories (${missing[*]}): $template_dir" >&2
        return 1
    fi

    return 0
}
