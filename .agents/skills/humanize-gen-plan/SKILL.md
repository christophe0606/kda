---
name: humanize-gen-plan
description: Generate a repository-grounded implementation plan from a draft, with independent Codex analysis and planner-reviewer deliberation. Planning only; does not implement the application change.
---

# Humanize Generate Plan

Run from the target project's root. Read AGENTS.md, preserve the draft, and change
only planning artifacts. The current Codex task is the planner; a separate Codex
CLI process is the reviewer. Two Codex models can deliberate: Codex-only does not
mean single-model planning. Never skip independent review because Claude is absent.

Use the model already running the current Codex task as the planner. Do not require
or request a model switch, and do not gate planning on a particular model name.
Select the independent reviewer from Humanize's merged `codex_model` and
`codex_effort` configuration, including `.humanize/config.json`. These settings
control only the reviewer, not the planner. Record the actual model identities
when known; an unknown planner identity does not block planning. Independent
review is still required even if the configured reviewer uses the same model as
the planner: run it as a separate Codex CLI process.

## Windows execution

Never use WSL, wsl.exe, or bare bash from PATH. Prefer this native launcher:

```powershell
uv run .agents/skills/humanize/run-humanize.py scripts/<script>.sh <arguments>
```

If uv is unavailable, use `python3` or `python` followed by the same `.py` file.
The launcher selects Git for Windows explicitly. It preserves stdin, stdout,
stderr, and exit status and invokes the existing Humanize runtime. Pass arguments
separately; never interpolate prompt text into shell source. Codex and jq come
from PATH. Reviewer calls require an authenticated Codex CLI.

## Arguments and mode

Required: `--input <draft>` and `--output <new-plan>`.
Optional: `--discussion`, `--direct`, `--auto-start-rlcr-if-converged`.
The two mode flags are mutually exclusive. Explicit flags override merged
`gen_plan_mode` configuration; its default is `discussion`.

```text
$humanize-gen-plan --input docs/draft.md --output docs/plan.md --discussion
```

The output must be a new file. Do not delete an existing plan to pass validation;
use the user's replacement authorization or a new output path.

## Required workflow

### 1. Validate and inspect

Run `scripts/validate-gen-plan-io.sh` through the native launcher with the parsed
arguments. Stop on nonzero status. Read the returned `TEMPLATE_FILE` and the
resolved `GEN_PLAN_MODE`, `CODEX_MODEL`, `CODEX_EFFORT`, and
`ALTERNATIVE_PLAN_LANGUAGE`. The validator uses Humanize's merged configuration:
runtime defaults, user settings, then project settings. Do not substitute a
hard-coded reviewer model for the effective configuration.

Read the draft and relevant application files. Check that the requested work is
relevant to this repository; report and stop if it is not. Resolve only material
ambiguities with the user. Preserve draft requirements and distinguish hard
quantitative requirements from trends when needed.

### 2. Independent first-pass analysis

Before synthesizing the candidate plan, write a UTF-8 prompt under a fresh
`.humanize/planning/<run>/` directory. Include the raw draft, repository context,
relevant paths, scope constraints, and a request to inspect files read-only.
Request `CORE_RISKS`, `MISSING_REQUIREMENTS`, `TECHNICAL_GAPS`,
`ALTERNATIVE_DIRECTIONS`, `QUESTIONS_FOR_USER`, and `CANDIDATE_CRITERIA`.
Tell the reviewer this is planning only and it must not edit application files.

Invoke the existing reviewer, passing the prompt as data:

```powershell
uv run .agents/skills/humanize/run-humanize.py scripts/ask-codex.sh --question-file .humanize/planning/<run>/analysis-prompt.md
```

This uses the configured Codex model and effort, disables nested hooks, and saves
input, output, and model/exit metadata under `.humanize/skill/`. Read the real
response and preserve its evidence paths. A nonzero exit, empty response, or
unavailable model is a failure, not reviewer agreement. Report the error and ask
whether to retry or explicitly continue without independent review; never silently
substitute self-review or another model. Do not start implementation.

### 3. Candidate plan

The current planner synthesizes candidate v1 from the draft, repository evidence,
and first-pass findings. Use the complete returned plan template, including
acceptance criteria with positive/negative tests, path boundaries, feasibility,
dependencies, milestones, and a task table tagged `coding` or `analyze`.
Keep the candidate and unresolved questions in the planning run directory.

### 4. Discussion rounds

In `discussion` mode, invoke `scripts/ask-codex.sh --question-file <round-prompt>`
for up to three sequential review/revision rounds. Each prompt must include the
entire current candidate, relevant draft requirements, prior reviewer findings,
planner responses, and unresolved disagreements. Request these response fields:

- `AGREE`: accepted points.
- `DISAGREE`: disputed points with reasons and impact.
- `REQUIRED_CHANGES`: blocking corrections, or `NONE`.
- `OPTIONAL_IMPROVEMENTS`: nonblocking suggestions.
- `UNRESOLVED`: decisions requiring the user, or `NONE`.

Read each independent response. Revise the candidate and record which suggestions
were accepted or rejected and why. Send material revisions back to the reviewer;
review of an earlier candidate does not establish agreement with a later one.
Record topic, planner position, reviewer position, and resolution per round.

Stop when the reviewed candidate has no required changes, no high-impact disputed
points, and no unresolved user decisions. Set `converged` only then. Stop as
`partially_converged` after three rounds or two consecutive rounds without material
progress. Any unreviewed final revisions also mean `partially_converged`. Carry
remaining decisions into `Pending User Decisions`; never invent agreement.

In `direct` mode, retain first-pass analysis but skip these convergence rounds.
Report `partially_converged` and explicitly say discussion rounds were skipped.
Direct mode cannot trigger automatic implementation.

### 5. Write and report

Write the final plan only after the analysis and applicable discussion rounds.
Use `## Codex-Codex Deliberation`, with actual planner/reviewer model identities,
project-relative evidence paths, agreements, resolved disagreements, and convergence
status. A failed or waived reviewer call must be recorded as such; it is not a
successful two-model deliberation. Keep uncertainty about an unknown planner model
explicit. The refine-plan validator also accepts the legacy Claude-Codex heading.

Keep pending user decisions visible with `DEC-N` identifiers and positions from
both roles. If an alternative language is configured, produce the corresponding
translated plan with the same decisions and criteria. Do not introduce application
changes, run a custom review loop, or modify Humanize state to imply completion.

Report output path, acceptance-criteria count, actual reviewer, evidence paths,
completed discussion rounds, convergence status, and pending decisions.

Only when `--auto-start-rlcr-if-converged` was explicitly requested, discussion
converged, and no user decisions remain, proceed through the installed
`humanize-rlcr` skill. Its hook trust and session binding requirements still apply.
Otherwise stop after planning; the Stop hook must exit successfully when no RLCR
loop is active. Planning deliberation comes from the calls above, not that hook.

## Validation failures

The I/O validator returns: 1 missing input, 2 empty input, 3 missing output
directory, 4 existing output, 5 unwritable output directory, 6 invalid arguments
or planning mode, and 7 missing template. Preserve the original draft on every
failure and report the actual error.
