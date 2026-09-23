---
name: humanize
description: Iterative development with AI review. Provides RLCR (Ralph-Loop with Codex Review) for implementation planning and code review loops.
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
argument. Do not paste user text into shell source. Read runtime files directly from PowerShell when needed.

Use Codex's available file/search/edit and user-question tools in place of Claude
tool names. `Task`/`Explore` means read-only Codex subagents, within the available
concurrency limit; queue additional directions in batches. Read AGENTS.md for
repository instructions. Claude Agent Teams is not a Codex feature.
Invoke flows using `$humanize-gen-idea`, `$humanize-gen-plan`,
`$humanize-refine-plan`, or `$humanize-rlcr`, not `/flow:` or Claude slash commands.
Use the current Codex task model for the lead role; do not require a model
switch. Where the workflow calls for independent review, use a separate Codex CLI
process with the merged Humanize `codex_model` and `codex_effort` configuration.
This does not add review rounds to workflows that do not have them upstream.
Use only Codex providers. Ask Codex requires a logged-in Codex CLI.
Never substitute fabricated reviews.
Before starting RLCR, the user must review/trust its Stop hook in Codex's `/hooks`
browser and use a session that has loaded it. Hook trust is not installed here.
The setup script uses CODEX_THREAD_ID to bind the loop to this Codex session.
If unavailable, pass --session-id with this session's actual ID; never invent one.
After code review passes, preserve the reviewed code during finalization.
Use this Codex session for the optional local methodology analysis.
For BitLesson selection, run scripts/bitlesson-select.sh through this launcher.

# Humanize - Iterative Development with AI Review

Humanize creates a feedback loop where AI implements your plan while another AI independently reviews the work, ensuring quality through continuous refinement.

## Runtime Root

The installer hydrates this skill with an absolute runtime root path:

```powershell
C:/Users/chrfav01/benchresults/humanize_tests/kda/.agents/skills/humanize
```

All command examples below use `C:/Users/chrfav01/benchresults/humanize_tests/kda/.agents/skills/humanize`.

## Core Philosophy

**Iteration over Perfection**: Instead of expecting perfect output in one shot, Humanize leverages an iterative feedback loop where:
- AI implements your plan
- Another AI independently reviews progress
- Issues are caught and addressed early
- Work continues until all acceptance criteria are met

## Available Workflows

### 1. RLCR Loop - Iterative Development with Review

The RLCR (Ralph-Loop with Codex Review) loop has two phases:

**Phase 1: Implementation**
- AI works on the implementation plan
- AI writes a summary of work completed
- Codex reviews the summary for completeness and correctness
- If issues found → feedback loop continues
- If Codex outputs "COMPLETE" → enters Review Phase

**Phase 2: Code Review**
- `codex review --base <branch>` checks code quality
- Issues marked with `[P0-9]` severity markers
- If issues found → AI fixes them and continues
- If no issues → loop completes with Finalize Phase
- On a current Codex CLI with `hooks` enabled, Humanize installs a native `Stop` hook so exit gating runs automatically after the user trusts the hook

### 2. Generate Plan - Structured Plan from Draft

Transforms a rough draft document into a structured implementation plan with:
- Clear goal description
- Acceptance criteria in AC-X format with TDD-style positive/negative tests
- Path boundaries (upper/lower bounds, allowed choices)
- Feasibility hints and conceptual approach
- Dependencies and milestone sequencing

## Commands Reference

### Start RLCR Loop

Read and follow `../humanize-rlcr/SKILL.md` for setup, round instructions, and finalization.

```powershell
# With a plan file
uv run .agents/skills/humanize/run-humanize.py scripts/setup-rlcr-loop.sh path/to/plan.md

# Or without plan (review-only mode)
uv run .agents/skills/humanize/run-humanize.py scripts/setup-rlcr-loop.sh --skip-impl
```

After each round, write the required summary and stop/exit normally. Humanize's native Codex `Stop` hook handles review gating automatically.

**Common Options:**
- `--max N` - Maximum iterations before auto-stop (default: 42)
- `--codex-model MODEL:EFFORT` - Reviewer model and reasoning effort for both review phases (default: merged Humanize configuration)
- Review phase `codex review` uses the reviewer model and effort saved at loop setup
- `--codex-timeout SECONDS` - Timeout for each Codex review (default: 5400)
- `--base-branch BRANCH` - Base branch for code review (auto-detects if not specified)
- `--full-review-round N` - Interval for full alignment checks (default: 5)
- `--skip-impl` - Skip implementation phase, go directly to code review
- `--track-plan-file` - Enforce plan-file immutability when tracked in git
- `--push-every-round` - Require git push after each round
- `--claude-answer-codex` - Let the current Codex task answer reviewer Open Questions directly (default: ask the user); the flag retains its upstream name
- `--agent-teams` is unsupported by the Codex-only runtime; do not pass it.
- `--yolo` - Skip Plan Understanding Quiz and enable --claude-answer-codex
- `--skip-quiz` - Skip the Plan Understanding Quiz only
- `--privacy` - Disable methodology analysis at loop exit (default: analysis enabled)

### Cancel RLCR Loop

```powershell
uv run .agents/skills/humanize/run-humanize.py scripts/cancel-rlcr-loop.sh
# or force cancel during finalize phase
uv run .agents/skills/humanize/run-humanize.py scripts/cancel-rlcr-loop.sh --force
```

### Generate Plan from Draft

```powershell
uv run .agents/skills/humanize/run-humanize.py scripts/validate-gen-plan-io.sh --input path/to/draft.md --output path/to/plan.md
```

Validation alone does not generate a plan. Read and follow `../humanize-gen-plan/SKILL.md` for the complete workflow, including independent analysis and planner-reviewer deliberation. The outline below is only a summary, not the full output schema.

### Ask Codex (One-shot Consultation)

```powershell
uv run .agents/skills/humanize/run-humanize.py scripts/ask-codex.sh [--codex-model MODEL:EFFORT] [--codex-timeout SECONDS] "your question"
```

## Plan File Structure

A good plan file should include:

```markdown
# Plan Title

## Goal Description
Clear description of what needs to be accomplished

## Acceptance Criteria

- AC-1: First criterion
  - Positive Tests (expected to PASS):
    - Test case that should succeed
  - Negative Tests (expected to FAIL):
    - Test case that should fail

## Path Boundaries

### Upper Bound (Maximum Scope)
Most comprehensive acceptable implementation

### Lower Bound (Minimum Scope)
Minimum viable implementation

### Allowed Choices
- Can use: technologies, approaches allowed
- Cannot use: prohibited technologies

## Dependencies and Sequence

### Milestones
1. Milestone 1: Description
   - Phase A: ...
   - Phase B: ...

## Implementation Notes
- Code should NOT contain plan terminology like "AC-", "Milestone", "Step"
```

## Goal Tracker System

The RLCR loop uses a Goal Tracker to prevent goal drift:

- **IMMUTABLE SECTION**: Ultimate Goal and Acceptance Criteria (set in Round 0, never changed)
- **MUTABLE SECTION**: Active Tasks, Completed Items, Deferred Items, Plan Evolution Log

### Key Principles

1. **Acceptance Criteria**: Each task maps to a specific AC
2. **Plan Evolution Log**: Document any plan changes with justification
3. **Explicit Deferrals**: Deferred tasks require strong justification
4. **Full Alignment Checks**: Every N rounds (default: 5), comprehensive goal alignment audit

## Important Rules

1. **Write summaries**: Always write work summary to the specified file before exiting
2. **Maintain Goal Tracker**: Keep goal-tracker.md up-to-date with progress
3. **Be thorough**: Include details about implementation, files changed, tests added
4. **No cheating**: Don't try to exit by editing state files or running cancel commands
5. **Use the native Stop hook on Codex**: After writing the required summary, stop/exit normally so Codex runs the Humanize Stop hook
6. **Trust the process**: External review helps improve implementation quality

## Prerequisites

- `codex` - OpenAI Codex CLI (for review)


## Directory Structure

Humanize stores all data in `.humanize/`:

```
.humanize/
├── rlcr/           # RLCR loop data
│   └── <timestamp>/
│       ├── state.md
│       ├── goal-tracker.md
│       ├── round-N-summary.md
│       ├── round-N-review-result.md
│       ├── finalize-state.md
│       ├── finalize-summary.md
│       ├── methodology-analysis-state.md
│       ├── methodology-analysis-report.md
│       ├── methodology-analysis-done.md
│       └── complete-state.md
└── skill/          # One-shot skill results
    └── <timestamp>/
        ├── input.md
        ├── output.md
        └── metadata.md
```

## Monitoring

Use the monitor script to track loop progress:

```powershell
source uv run .agents/skills/humanize/run-humanize.py scripts/humanize.sh
humanize monitor rlcr   # Monitor RLCR loop
```

## Exit Codes

### ask-codex.sh
- `0` - Success
- `1` - Validation error
- `124` - Timeout

### validate-gen-plan-io.sh
- `0` - Success
- `1` - Input file not found
- `2` - Input file is empty
- `3` - Output directory does not exist
- `4` - Output file already exists
- `5` - No write permission
- `6` - Invalid arguments
- `7` - Plan template file not found
