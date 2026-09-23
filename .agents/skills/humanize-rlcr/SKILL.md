---
name: humanize-rlcr
description: Start RLCR (Ralph-Loop with Codex Review) on Codex using the native Stop hook.
---

## Codex on Windows

Run this skill from the user's target project directory. Execute runtime scripts
with the native Windows launcher (pass each argument separately):

```powershell
uv run .agents/skills/humanize/run-humanize.py 'scripts/<script>.sh' 'argument'
```

Never use WSL or bare bash from PATH. If uv is unavailable, use python3 or python
to run the same .py launcher. It selects Git for Windows explicitly and resolves
jq, Python,
GNU utilities and Codex without changing the global PATH. For monitoring replace
the script argument with `monitor` and pass `rlcr`, `skill`, or `codex`.
Replace `$ARGUMENTS` with parsed user arguments, preserving quoted text as a single
argument. Do not paste user text into shell source. Runtime file paths below are
absolute and may also be read directly from PowerShell.

Use Codex's available file/search/edit and user-question tools in place of Claude
tool names. `Task`/`Explore` means read-only Codex subagents, within the available
concurrency limit; queue additional directions in batches. Read AGENTS.md for
repository instructions. Claude Agent Teams is not a Codex feature.
Invoke flows using `$humanize-gen-idea`, `$humanize-gen-plan`,
`$humanize-refine-plan`, or `$humanize-rlcr`, not `/flow:` or Claude slash commands.
Codex implements and a separate Codex CLI process independently reviews it.
Use only Codex providers. Ask Codex requires a logged-in Codex CLI.
Never substitute fabricated reviews.
Before starting RLCR, the user must review/trust its Stop hook in Codex's `/hooks`
browser and use a session that has loaded it. Hook trust is not installed here.
The setup script uses CODEX_THREAD_ID to bind the loop to this Codex session.
If unavailable, pass --session-id with this session's actual ID; never invent one.
After code review passes, preserve the reviewed code during finalization.
Use this Codex session for the optional local methodology analysis.
For BitLesson selection, run scripts/bitlesson-select.sh through this launcher.

# Humanize RLCR Loop

Use this flow as the Codex entrypoint for RLCR.
Codex installs of Humanize require native hooks support and install the Humanize `Stop` hooks automatically.

## Runtime Root

The installer hydrates this skill with an absolute runtime root path:

```bash
C:/Users/chrfav01/benchresults/humanize_tests/kda/.agents/skills/humanize
```

All commands below assume `C:/Users/chrfav01/benchresults/humanize_tests/kda/.agents/skills/humanize`.

## Model roles

The model running the current Codex task implements the plan. Do not require a
particular implementer model or ask the user to switch models. A separate Codex
CLI process reviews each round; its model and effort come from Humanize's merged
`codex_model` and `codex_effort` configuration, unless overridden by
`--codex-model MODEL:EFFORT`. Setup saves those reviewer settings in the loop state;
changing the configuration afterward does not change that active loop.

Both progress review (`codex exec`) and final code review (`codex review`) use the
saved reviewer settings. Reviewer feedback returns to this same implementing task
through the native Stop hook. Never replace that review with self-review, even if
both processes happen to use the same model.

In legacy generated prompts, "Claude" means this implementing Codex task, not a
separate provider. `coding` tasks belong to the current task; `analyze` tasks use
the configured independent Codex reviewer through the installed ask-codex runtime.
No Claude process or Claude Agent Teams is required or supported in this setup.

## Required Sequence

### 1. Setup

Start the loop with the setup script:

```bash
uv run .agents/skills/humanize/run-humanize.py scripts/setup-rlcr-loop.sh <parsed arguments>
```

If setup exits non-zero, stop and report the error.

For Codex-only installations, setup binds the loop to `CODEX_THREAD_ID`. If that
environment variable is unavailable, pass `--session-id` with the implementing
session's actual ID. Do not invent an ID or start an unbound loop. Use the same
session for subsequent rounds. The reviewer runs as a separate Codex CLI process
with its own hooks disabled.

### 2. Work Round

For each round:

1. Read current loop prompt from `.humanize/rlcr/<timestamp>/round-<N>-prompt.md` (or `finalize` prompt files when in finalize phase).
2. Implement required changes.
3. Commit changes.
4. Write required summary file:
   - Normal phase: `.humanize/rlcr/<timestamp>/round-<N>-summary.md`
   - Finalize phase: `.humanize/rlcr/<timestamp>/finalize-summary.md`
5. Stop or exit normally.
6. Let the native Humanize `Stop` hook run automatically.
7. If the hook blocks exit, follow the returned instructions exactly and continue the next round.

In Codex-only mode, finalization writes the final report without changing the
reviewed code. Optional methodology analysis runs in the current Codex session
and stays local. No Claude agent, Gemini CLI, or extra plugin is needed.

## What This Enforces

The native Stop-hook path enforces:

- state/schema validation (`current_round`, `max_iterations`, `review_started`, `base_branch`, etc.)
- branch consistency checks
- plan-file integrity checks (when applicable)
- incomplete Task/Todo blocking
- git-clean requirement before exit
- `--push-every-round` unpushed-commit blocking
- summary presence checks
- max-iteration handling
- full-alignment rounds (`--full-review-round`)
- strict `COMPLETE`/`STOP` marker handling
- review-phase transition guard (`.review-phase-started` marker)
- code-review gating on `[P0-9]` markers
- hard blocking on codex review failure or empty output
- open-question handling when `ask_codex_question=true`

## Critical Rules

1. Never manually edit `state.md` or `finalize-state.md`.
2. Never skip a blocked hook result by declaring completion manually.
3. Never run ad-hoc `codex exec` / `codex review` in place of the hook-managed phase transitions.
4. Always use files generated by the loop (`round-*-prompt.md`, `round-*-review-result.md`) as source of truth.

## Options

Pass these through `setup-rlcr-loop.sh`:

| Option | Description | Default |
|--------|-------------|---------|
| `path/to/plan.md` | Plan file path | Required unless `--skip-impl` |
| `--plan-file <path>` | Explicit plan path | - |
| `--track-plan-file` | Enforce tracked plan immutability | false |
| `--max N` | Maximum iterations | 42 |
| `--codex-model MODEL:EFFORT` | Reviewer model and effort for both review phases | merged Humanize configuration |
| `--codex-timeout SECONDS` | Codex timeout | 5400 |
| `--base-branch BRANCH` | Base for review phase | auto-detect |
| `--full-review-round N` | Full alignment interval | 5 |
| `--skip-impl` | Start directly in review path | false |
| `--push-every-round` | Require push each round | false |
| `--claude-answer-codex` | Let the implementing task answer open questions (legacy option name) | false |
| `--agent-teams` | Unsupported in Codex-only mode; do not pass | false |
| `--yolo` | Skip quiz and enable --claude-answer-codex | false |
| `--skip-quiz` | Skip Plan Understanding Quiz (implicit in skill mode) | false |

The reviewer model and effort are saved at setup; they do not select the implementing task's model.

## Usage

```bash
# Start with plan file
$humanize-rlcr path/to/plan.md

# Review-only mode
$humanize-rlcr --skip-impl
```

## Cancel

```bash
uv run .agents/skills/humanize/run-humanize.py scripts/cancel-rlcr-loop.sh
```
