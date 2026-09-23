---
name: humanize-refine-plan
description: Refine an annotated implementation plan into a comment-free plan and a QA ledger while preserving the gen-plan schema.
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

# Humanize Refine Plan

Read [the full refinement workflow](references/workflow.md) before executing.
It preserves the original annotation-processing, configuration, QA, convergence,
and staged-write behavior. The sections below are a summary. The current Codex
task performs refinement; this workflow does not introduce independent review
rounds. Record review history accurately rather than implying a fresh review.

Refines an annotated plan that contains `CMT:` / `ENDCMT` blocks into a comment-free plan plus a QA ledger, while preserving the `gen-plan` structure and convergence state.

The installer hydrates this skill with an absolute runtime root path:

```powershell
C:/Users/chrfav01/benchresults/humanize_tests/kda/.agents/skills/humanize
```

```mermaid
flowchart TD
    BEGIN([BEGIN]) --> SETUP[Parse arguments and derive paths<br/>Resolve mode, output path, QA path, alt-language]
    SETUP --> LOAD_CFG[Load merged config<br/>Reuse humanize config precedence and defaults]
    LOAD_CFG --> VALIDATE[Validate IO<br/>Run through native launcher: scripts/validate-refine-plan-io.sh --input &lt;annotated-plan&gt; [--output ...] [--qa-dir ...] [--discussion|--direct]]
    VALIDATE --> VALID_OK{Validation passed?}
    VALID_OK -->|No| REPORT_VALIDATION[Report validation error<br/>Stop]
    REPORT_VALIDATION --> END_FAIL([END])
    VALID_OK --> EXTRACT[Read input plan and extract valid<br/>CMT:/ENDCMT blocks with a stateful scanner]
    EXTRACT --> PARSE_OK{Parse succeeded?}
    PARSE_OK -->|No| REPORT_PARSE[Report parse error with<br/>line, column, heading, context<br/>Stop]
    REPORT_PARSE --> END_FAIL
    PARSE_OK --> CLASSIFY[Classify comments:<br/>question, change_request, research_request]
    CLASSIFY --> AMBIG{Ambiguous comments?}
    AMBIG -->|Yes, discussion mode| ASK_USER[Ask the minimum user question<br/>needed to continue]
    ASK_USER --> PROCESS
    AMBIG -->|No| PROCESS[Process comments in order:<br/>answer, refine plan, or do targeted repo research]
    PROCESS --> REFINE[Generate refined plan text<br/>Keep required gen-plan sections intact]
    REFINE --> PLAN_CHECK{Plan still valid?<br/>No CMT markers, references consistent,<br/>routing tags valid}
    PLAN_CHECK -->|No, fixable| FIX[Repair internal inconsistencies]
    FIX --> PLAN_CHECK
    PLAN_CHECK -->|No, blocking| REPORT_BLOCK[Report blocking inconsistency<br/>Stop]
    REPORT_BLOCK --> END_FAIL
    PLAN_CHECK -->|Yes| QA[Populate QA document from<br/>C:/Users/chrfav01/benchresults/humanize_tests/kda/.agents/skills/humanize/prompt-template/plan/refine-plan-qa-template.md]
    QA --> ALT_LANG{Generate translated variants?}
    ALT_LANG -->|Yes| VARIANTS[Translate refined plan and QA<br/>Keep identifiers unchanged]
    ALT_LANG -->|No| ATOMIC
    VARIANTS --> ATOMIC[Write refined plan, QA, and variants<br/>atomically via temp files]
    ATOMIC --> REPORT_SUCCESS[Report success:<br/>paths, counts, mode, convergence status]
    REPORT_SUCCESS --> END_SUCCESS([END])
```

## Input Requirements

**Required Arguments:**
- `--input <path/to/annotated-plan.md>` - Input plan that already follows the `gen-plan` schema and contains at least one comment block (`CMT:` / `ENDCMT`, `<cmt>`, or `<comment>`)

**Optional Arguments:**
- `--output <path/to/refined-plan.md>` - Output path for the refined plan; defaults to in-place mode (`--input`)
- `--qa-dir <path/to/qa-dir>` - Directory for the generated QA ledger; defaults to `.humanize/plan_qa`
- `--alt-language <language-or-code>` - Optional translated output language for plan and QA variants
- `--discussion` - Ask the user to resolve ambiguous classifications or language decisions
- `--direct` - Resolve ambiguity with the smallest safe assumption and record it in QA

**Argument Rules:**
- `--discussion` and `--direct` are mutually exclusive
- The validator does not accept `--alt-language`, so do not pass that flag to `validate-refine-plan-io.sh`
- If `--output` is omitted, refine the plan in place and still write the QA document separately

## Workflow Guarantees

The refinement flow must:

- Preserve the `gen-plan` schema instead of inventing new top-level sections
- Remove all resolved `CMT:` / `ENDCMT` blocks from the final plan
- Keep required sections intact:
  - `## Goal Description`
  - `## Acceptance Criteria`
  - `## Path Boundaries`
  - `## Feasibility Hints and Suggestions`
  - `## Dependencies and Sequence`
  - `## Task Breakdown`
  - `## Codex-Codex Deliberation` (also accept and preserve legacy `## Claude-Codex Deliberation`)
  - `## Pending User Decisions`
  - `## Implementation Notes`
- Preserve optional sections when present, including the original design draft appendix
- Keep task routing tags restricted to `coding` or `analyze`
- Generate a QA ledger from the shipped QA template
- Write the refined plan, QA file, and any language variants atomically

## Classification And Output

Each extracted raw comment block receives one dominant classification:

- `question`
- `change_request`
- `research_request`

The flow produces:

- A refined plan with comment blocks removed and approved refinements applied
- A QA ledger that records:
  - one row per raw `CMT-N`
  - classification and disposition
  - answers to questions
  - research findings
  - applied plan changes
  - remaining decisions
  - refinement metadata and convergence status

## Supported Alternate Languages

`--alt-language` supports these normalized values:

| Language | Code | Variant Suffix |
|----------|------|----------------|
| Chinese | `zh` | `_zh` |
| Korean | `ko` | `_ko` |
| Japanese | `ja` | `_ja` |
| Spanish | `es` | `_es` |
| French | `fr` | `_fr` |
| German | `de` | `_de` |
| Portuguese | `pt` | `_pt` |
| Russian | `ru` | `_ru` |
| Arabic | `ar` | `_ar` |

Rules:

- Accept either the language name or ISO code
- Treat `English` / `en` as a no-op
- Keep identifiers unchanged in translated variants
- If the alternate language matches the main plan language, skip variant generation

## Validation Exit Codes

| Exit Code | Meaning |
|-----------|---------|
| 0 | Success - continue |
| 1 | Input file not found |
| 2 | Input file is empty |
| 3 | Input file has no `CMT:` blocks |
| 4 | Input file is missing required `gen-plan` sections |
| 5 | Output directory does not exist or is not writable |
| 6 | QA directory is not writable |
| 7 | Invalid arguments |

## Usage

```text
$humanize-refine-plan --input docs/plan.md
$humanize-refine-plan --input docs/plan.md --output docs/refined-plan.md --direct
```
