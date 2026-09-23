---
name: ask-codex
description: Consult Codex as an independent expert. Sends a question or task to codex exec and returns the response.
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

# Ask Codex

Send a question or task to Codex and return the response.

## How to Use

Do not pass free-form user text to the shell unquoted. The question or task may contain spaces or shell metacharacters such as `(`, `)`, `;`, `#`, `*`, or `[`.

Pass parsed flags separately and the question as one quoted argument:

```powershell
uv run .agents/skills/humanize/run-humanize.py scripts/ask-codex.sh 'Review the error handling in src/'
```

For multiline or shell-sensitive text, write the exact question to a UTF-8 file
and use the existing file-input option:

```powershell
uv run .agents/skills/humanize/run-humanize.py scripts/ask-codex.sh --question-file build/question.md
```

Preserve explicit `--codex-model MODEL:EFFORT` and `--codex-timeout SECONDS`
overrides supplied by the user. Otherwise use merged Humanize configuration;
do not force a model or change the current task's model. This is one consultation,
not an RLCR loop, and does not require installing or trusting a Stop hook.

## Interpreting Output

- The script outputs Codex's response to **stdout** and status info to **stderr**
- Read the stdout output carefully and incorporate Codex's response into your answer
- If the script exits with a non-zero code, report the error to the user

## Error Handling

| Exit Code | Meaning |
|-----------|---------|
| 0 | Success - Codex response is in stdout |
| 1 | Validation error (missing codex, empty question, invalid flags) |
| 124 | Timeout - suggest using `--codex-timeout` with a larger value |
| Other | Codex process error - report the exit code and any stderr output |

## Notes

- The response is saved to `.humanize/skill/<timestamp>/output.md` for reference
- Model and effort come from merged Humanize configuration; timeout defaults to 3600 seconds.
