#!/usr/bin/env bash
set -euo pipefail
RUNTIME_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$RUNTIME_ROOT/windows-bin:/usr/bin:/bin:$PATH"
export HUMANIZE_PROVIDER_MODE=codex-only
export PYTHONUTF8=1
# Keep ancillary prompts and diagnostics scoped to this installation's project.
PROJECT_ROOT="$(cd "$RUNTIME_ROOT/../../.." && pwd)"
export HUMANIZE_CONFIG="$PROJECT_ROOT/.humanize/config.json"
export XDG_CACHE_HOME="$PROJECT_ROOT/.humanize/cache"
if [[ $# -eq 0 ]]; then
    echo 'Usage: run-humanize.sh scripts/<script>.sh [arguments...]' >&2
    exit 1
fi
if [[ "$1" == monitor ]]; then
    shift
    source "$RUNTIME_ROOT/scripts/humanize.sh"
    humanize monitor "$@"
    exit $?
fi
script="$1"
shift
if [[ "$script" != scripts/*.sh && "$script" != hooks/*.sh ]]; then
    echo 'Expected a Humanize scripts/*.sh or hooks/*.sh entrypoint.' >&2
    exit 1
fi
exec "$BASH" --noprofile --norc "$RUNTIME_ROOT/$script" "$@"
