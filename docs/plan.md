# Personalized Greeting Implementation Plan

## Goal Description

Extend the existing `hello` application to accept zero or one name argument and reject excess arguments. Preserve the default greeting, portable C11 implementation, deterministic output, and existing CMake/CTest workflow. This document plans the change in `docs/draft.md`; application implementation starts only through a separately invoked Humanize implementation/review workflow.

Repository baseline: `src/main.c` uses `main(void)` and `puts`; `CMakeLists.txt` already requires C11 without extensions and registers `hello_default`; `tests/check_output.cmake` runs the built executable, captures both streams/status, and checks the default greeting after CRLF normalization.

## Acceptance Criteria

Positive tests below must pass. Negative tests describe rejected inputs or faulty outcomes that the assertions must detect; they do not require committing broken code or adding a mutation-test framework. Each runtime case invokes the built application directly and checks exit status, stdout, and stderr together.

- AC-1: Preserve no-argument behavior.
  - Positive Tests (expected to PASS): no arguments return zero, stdout is exactly `Hello, embedded!\n`, and stderr is empty.
  - Negative Tests (expected to FAIL): the checker rejects missing/extra newline, changed greeting, additional output, any stderr, or nonzero status.
- AC-2: Greet a supplied single name literally.
  - Positive Tests (expected to PASS): `Alice` returns zero with exactly `Hello, Alice!\n` on stdout and empty stderr. One quoted argument `Ada Lovelace 100% %s%n` returns zero with exactly `Hello, Ada Lovelace 100% %s%n!\n` and empty stderr. An explicit empty argument returns zero with `Hello, !\n` and empty stderr.
  - Negative Tests (expected to FAIL): the checker rejects splitting/truncating the special name, interpreting percent sequences as formatting, treating an empty argument as absent, extra output, any stderr, or a nonzero status. Names are not subject to added length or character restrictions.
- AC-3: Reject more than one user argument before printing a greeting.
  - Positive Tests (expected to PASS): two arguments `Alice` and `Bob`, and three arguments `Alice`, `Bob`, and `Carol`, each yield a nonzero numeric process exit status, empty stdout, and exactly `Usage: hello [name]\n` on stderr.
  - Negative Tests (expected to FAIL): the checker rejects zero status, any stdout, missing/wrong usage text, or a timeout/launch error being mistaken for an expected application failure. It must not merely compare a result string as unequal to `0`.
- AC-4: Preserve the implementation constraints.
  - Positive Tests (expected to PASS): the Release build succeeds with the existing `hello` target and C11 settings; source review confirms use of argument pointers and standard-library output with literal format strings or separate output calls.
  - Negative Tests (expected to FAIL): review rejects dynamic allocation, a fixed-size name buffer, format strings derived from input, platform APIs, extra dependencies, SDK/RTOS support, or changes to Humanize itself.
- AC-5: Deliver executable regression coverage in the existing CTest workflow.
  - Positive Tests (expected to PASS): CTest runs all six cases in AC-1 through AC-3 against `$<TARGET_FILE:hello>`; checks exact streams and status; preserves spaces, percent sequences, and the explicit empty argument; normalizes only CRLF to LF in both captured streams while retaining final newlines. `hello_default` remains registered.
  - Negative Tests (expected to FAIL): reject tests that only inspect source or mock greetings, silently skip cases, strip output whitespace, use substring-only matching, lose empty arguments through list expansion, or let missing executables/timeouts pass. A nonexistent executable path must fail the checker.

## Path Boundaries

### Upper Bound (Maximum Acceptable Scope)

Implement the three argument-count branches in `src/main.c`; extend the CMake test script and, if needed, test registration in `CMakeLists.txt` for all six cases with clear diagnostics. A small CMake assertion helper is acceptable. A brief correction to README usage/baseline wording is allowed during implementation if necessary for accuracy. Keep the project small.

### Lower Bound (Minimum Acceptable Scope)

All five criteria and six runtime cases are satisfied using direct argument access and the current CMake-script testing approach. Separate named CTest registrations are optional; the existing `hello_default` registration may invoke a script that checks all cases.

### Allowed Choices

- Can use: standard C11 `argc`/`argv`, `puts`, `fputs`, or a literal `printf` format with `%s`; return zero for success and a standard nonzero failure value such as `EXIT_FAILURE`.
- Can use: existing CMake 3.20+ script facilities and CTest; either explicit case invocations with shared assertions or named case selection with literal arguments inside the script. Invoke the executable directly without a shell. Quote the executable path.
- Fixed decisions: default and personalized greeting bytes as specified; a fixed usage line `Usage: hello [name]\n`; no filtering of names; an explicit empty argument is a valid name. Platform text-stream CRLF is accepted as the logical newline by tests.
- Cannot use: allocation, fixed-size name buffers, new frameworks/dependencies, argument parsing libraries, custom review orchestration, or performance/hardware extensions.
- Implementation files: `src/main.c`, `tests/check_output.cmake`, `CMakeLists.txt`, and narrowly relevant `README.md` usage text. Preserve `docs/draft.md`, `AGENTS.md`, unrelated existing edits, and machine-local skill/config files. Builds belong in `build/`. Planning changes are limited to `docs/plan.md` and ignored `.humanize/planning/` evidence; the existing reviewer records its evidence in ignored `.humanize/skill/`.

## Feasibility Hints and Suggestions

These are conceptual suggestions rather than additional requirements.

### Conceptual Approach

Use `int main(int argc, char *argv[])`. Check for `argc > 2` first and write the fixed usage line to stderr before returning failure. Otherwise choose the default literal when `argc == 1`, or `argv[1]` when `argc == 2`, and emit the greeting with a literal format string. This uses the supplied argument without a copy or buffer and naturally supports an empty name.

For tests, extend the current script with explicit `execute_process` calls or a case selector that constructs each literal command inside the script. Capture all outputs and use the current five-second per-process timeout. Keep empty arguments explicit (`COMMAND "${HELLO_EXE}" ""`) instead of forwarding an unquoted argument list. Avoid joining names into a command string. Normalize both streams with CRLF-to-LF replacement, without stripping whitespace. Require status `0` for success; for rejection first require a numeric result and then require it to be nonzero. Process launch/timeout diagnostics are harness failures.

Use the existing generator expression to locate the target across single- and multi-configuration generators. No need to hard-code `build/Release/hello.exe`. This is a small change requiring only CMake and the already available host C compiler.

### Relevant References

- `docs/draft.md`: behavior, constraints, and required acceptance evidence.
- `AGENTS.md`: portable implementation and required build/test commands; planning-only and Humanize boundaries.
- `src/main.c`: current default greeting.
- `CMakeLists.txt`: target, C standard, warnings, and test registration.
- `tests/check_output.cmake`: current process capture, timeout, and newline comparison.
- `README.md`: local build commands and separate planning/implementation workflow.

## Dependencies and Sequence

### Milestones

1. Establish executable regression cases.
   - Extend the test script/registration for all six runtime cases and each stream/status contract.
   - Build the baseline and run CTest: the default case should pass while new behavior should fail for the expected behavioral reasons. Ensure no quoting or launch failure is mistaken for a meaningful regression failure.
2. Implement and verify the greeting behavior.
   - Add argument handling and deterministic usage output while preserving constraints.
   - Run, from the repository root:
     ```text
     cmake -S . -B build
     cmake --build build --config Release
     ctest --test-dir build -C Release --output-on-failure
     ```
   - Confirm all six runtime cases ran and passed. Exercise the checker's nonexistent-executable failure path and record its expected nonzero outcome. Inspect source and diff for scope and format-string safety. Update only directly obsolete README usage text if needed.
3. Complete the installed Humanize implementation/review workflow when the user separately invokes it.
   - Submit actual test evidence for independent review using the installed workflow and its native Stop hook. Record failures or unavailable checks accurately; do not implement a replacement review loop.

Test additions precede implementation. Final independent review depends on implemented behavior and actual build/CTest evidence. No performance benchmark, timing threshold, or hardware verification is required. Planning itself does not run or claim implementation tests.

## Task Breakdown

Each task has exactly one routing tag. `coding` belongs to the implementing Codex session; `analyze` denotes independent review via the installed Humanize `scripts/ask-codex.sh` capability. When RLCR is active, follow its generated round instructions and native Stop hook; this table does not replace the hook.

| Task ID | Description | Target AC | Tag (`coding`/`analyze`) | Depends On |
|---------|-------------|-----------|-------------------------|------------|
| task1 | Extend executable tests for all six cases, exact stream/status checks, argument preservation, and launch-error rejection; establish expected baseline failures. | AC-1, AC-2, AC-3, AC-5 | coding | - |
| task2 | Implement argument-count branching, literal greeting output, and fixed usage output without allocation or buffers. | AC-1, AC-2, AC-3, AC-4 | coding | task1 |
| task3 | Run the documented Release build and CTest, check nonexistent-executable rejection, inspect scope, and align any obsolete README usage wording. Record actual results. | AC-1, AC-2, AC-3, AC-4, AC-5 | coding | task2 |
| task4 | Independently assess final behavior, tests, constraints, and evidence under the installed implementation/review workflow. | AC-1, AC-2, AC-3, AC-4, AC-5 | analyze | task3 |

## Codex-Codex Deliberation

- Planner: current Codex session; exact model identity unavailable.
- Reviewer: `gpt-5.6-luna`, effort `high`, confirmed by first-pass metadata.
- Mode: `discussion`; no automatic implementation requested.
- Evidence directory: `.humanize/planning/20260923-112833/` (first-pass prompt, candidates, discussion prompts and ledger, prior plan backup).
- First-pass evidence: `.humanize/skill/2026-09-23_11-29-01-7954-e397c684/input.md`, `output.md`, and `metadata.md` (exit 0, success).
- Round 1 evidence: `.humanize/skill/2026-09-23_11-31-51-8034-8b4f16b7/input.md`, `output.md`, and `metadata.md` (exit 0, success; reviewer `gpt-5.6-luna`, effort `high`). The complete reviewed candidate is `.humanize/planning/20260923-112833/candidate-v1.md`; topic-by-topic positions and resolutions are in `.humanize/planning/20260923-112833/discussion-ledger.md`.

### Agreements

- First-pass analysis supports direct `argv` use, literal formatting, exact stream/status checks, direct process invocation, both-stream newline normalization, and keeping scope inside the application.
- The planner adopts the reviewer's recommended empty-name interpretation and fixed usage message as routine choices consistent with the draft; they do not require additional product decisions.

### Resolved Disagreements

- No material disagreement in first-pass analysis. The reviewer offered a shared-script approach or separate named tests; the planner allows either while retaining `hello_default` and requiring all six cases.
- Round 1 accepted all implementation requirements with REQUIRED_CHANGES: NONE and UNRESOLVED: NONE. The optional clarification about shared-script reporting is recorded: all six runtime cases may run within the single `hello_default` CTest entry. The optional suggestion to drop mandatory missing-executable validation was declined because that inexpensive check demonstrates the required rejection of harness errors. The reviewed requirement is retained unchanged.

### Convergence Status

- Final Status: `converged`. One discussion round completed; no blocking corrections, high-impact disputes, or unresolved user decisions. No implementation requirements changed after the reviewed candidate; final changes record only deliberation results and evidence.

## Pending User Decisions

None. Empty-name behavior and usage wording are resolved as above from the draft and reviewer recommendations; the discussion reviewer reported UNRESOLVED: NONE. No pending DEC-N entries remain.

## Implementation Notes

### Code Style Requirements

- Implementation code and comments must not contain plan-specific workflow markers such as `AC-`, `Milestone`, `Step`, or `Phase`.
- Use descriptive, domain-appropriate names in implementation and tests.
- Preserve pre-existing changes in `AGENTS.md` and `README.md`; neither is a clean baseline in this planning session.
- Current planning work does not change application code or start an RLCR loop. Actual build/test results belong to the subsequent implementation evidence.

## Output File Convention

Main output: `docs/plan.md`, replacing the prior file with explicit user authorization. Prior output is preserved at `.humanize/planning/20260923-112833/previous-plan.md`. Preserve the source draft.

### Translated Language Variant

Merged `ALTERNATIVE_PLAN_LANGUAGE` is empty; no translated variant is required. If configured in a future generation, use the skill's `plan_<code>.md` naming with matching criteria, decisions, paths, and identifiers. Do not change configuration to add a translation.
