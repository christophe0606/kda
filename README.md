# KDA: a minimal application for Humanize

A portable C11 Hello World application for trying the existing Humanize skills.
The application is the root project. Humanize supplies planning and independent
review. No NVIDIA dependencies, embedded knowledge base, or custom flow is included.

The application implements the personalized greeting described in `docs/draft.md`.
It accepts one optional name and rejects excess arguments.

## Recommended starting point: Codex CLI

Use **Codex CLI for the first complete run**. Your customized Humanize installation
uses a native Stop hook to trigger review. The CLI exposes `/hooks` so you can check
and trust that hook, and it is also needed to run the separate reviewer process.
Both the implementer and reviewer can use Codex with your Codex-only installation.

You can use the desktop app after the local installation is working; see below.
These instructions use the skills from the PolyArch Humanize checkout, not the
`hmz` runner's `humanize1` flows.

## 1. Open the project and check the baseline

Open PowerShell in the `kda` folder. All file paths and commands in this document
are relative to that folder:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

You need CMake and a host C compiler. On Windows, Visual Studio with the C++ desktop
build tools supplies the C compiler; CMake can select its generator. No board or
cross-compiler is needed. With the Visual Studio generator, run:

```powershell
.\build\Release\hello.exe
```

It prints `Hello, embedded!`. With a single-configuration generator, the executable
is usually `build/hello.exe` on Windows or `build/hello` on Unix.

Pass one name to personalize the greeting:

```powershell
.\build\Release\hello.exe 'Ada Lovelace 100% %s%n'
```

This prints `Hello, Ada Lovelace 100% %s%n!`. Names are used literally; an explicit
empty argument produces `Hello, !`. More than one argument prints
`Usage: hello [name]` to standard error and exits with a nonzero status.

The `kda` folder must be **its own Git repository**. If a local `.git` does not
exist yet, initialize and commit the baseline once:

```powershell
git init -b main
git add CMakeLists.txt src tests AGENTS.md README.md .gitignore docs/draft.md
git commit -m "chore: add minimal Humanize application"
```

Check `git rev-parse --show-toplevel`: it must resolve to this `kda` folder.
Keep future work and commits inside this repository.

## 2. Install the existing Humanize skills separately

Humanize is installed separately from the application. Use this `kda` folder as
the installation target; the application's build does not install skills.

The expected local installation includes:

```text
.agents/skills/humanize/            shared runtime and native Windows launcher
.agents/skills/humanize-gen-plan/   planning skill
.agents/skills/humanize-rlcr/       implementation/review skill
.codex/hooks.json                  review Stop hook
.humanize/config.json              Humanize configuration
```

Copying skill descriptions alone is insufficient: Humanize also needs its runtime
and review hook. Git ignores these machine-local files. Follow Humanize's own
prerequisites for Git Bash, Python, jq, and Codex CLI.

This project's installed Humanize runtime prefers `uv` from `PATH`: Python script
files run as `uv run script.py`, while inline code and stdin programs use
`uv run python -c ...` and `uv run python - ...`. Without uv, it uses `python3`,
then `python`, from `PATH`. No local `python3` wrapper is needed. A failed invocation
is returned to the caller rather than retried with another interpreter.

Codex and jq are resolved directly from `PATH`; neither has a local wrapper or
bundled executable. The environment's jq was verified to produce LF-only output
for Humanize's shell parsing. `windows-bin` contains only the Humanize-specific
`bitlesson-selector` helper. Ensure Codex, jq, and uv or Python are available to
Git Bash and Codex's hook environment. Reinstalling the skills may overwrite these
local runtime customizations.

Never use WSL. The Windows hook and installed skills enter through
`.agents/skills/humanize/run-humanize.py`, normally with `uv run`. This launcher
selects Git for Windows by its installation path and invokes the existing Bash
runtime without resolving bare `bash` from `PATH`. If Git is installed elsewhere,
set `HUMANIZE_GIT_BASH` to its `bash.exe`; the launcher checks that it belongs to a
Git installation. Without uv, run the same launcher with native `python3` or
`python`. The hook installer selects the available Python command when installed.

## 3. Start Codex CLI and verify the integration

In PowerShell, from this project:

```powershell
codex login
codex features list | Select-String '^hooks\s'
codex
```

If already authenticated, you can skip `codex login`. Your Humanize installer
expects hooks to be enabled; if that check fails, resolve it using the Humanize
installation instructions before running the loop.

Inside Codex, trust this project when prompted. Use `/skills` to check that
`humanize-gen-plan` and `humanize-rlcr` are available. Use `/hooks` to inspect and
trust Humanize's Stop hook. Installing a hook definition does not grant it trust.
Start a fresh session after installation if the skills have not appeared.

## 4. Generate the plan

The model already running the current Codex task is the planner; no model switch
is required. Configure the independent reviewer in `.humanize/config.json`, for
example:

```json
{
  "codex_model": "gpt-5.6-luna",
  "codex_effort": "high",
  "gen_plan_mode": "discussion"
}
```

Merge these settings with any existing configuration. They select only the
reviewer, not the planner. In discussion mode it calls the reviewer for first-pass
analysis, then performs up to three candidate review/revision rounds. The final
plan records actual models, agreements, disagreements, and review evidence under
`.humanize/skill/`. A Codex-only setup still uses two independent model processes.

Enter this in the **Codex conversation**, not PowerShell:

```text
$humanize-gen-plan --input docs/draft.md --output docs/plan.md --discussion
```

This should generate a plan, not implement the change. Read `docs/plan.md` and
resolve any questions. When satisfied, commit the plan from another terminal:

```powershell
git add docs/plan.md
git commit -m "docs: add personalized greeting plan"
git status --short
```

The working tree should be clean before starting the loop. Do not reuse an existing
plan output blindly: select a new output path when running a new planning exercise.

## 5. Implement and review

In the Codex conversation:

```text
$humanize-rlcr docs/plan.md --track-plan-file --max 3
```

The three-round limit bounds this first experiment; reaching it is not proof of
completion. The implementing session changes the code and runs tests. When it
tries to finish, the Stop hook invokes an independent Codex reviewer and returns
feedback. Continue in the same session for subsequent rounds.

The installed skill handles loop setup and writes state, summaries, and reviews
under `.humanize/rlcr/`. Do not manually advance that state or launch a replacement
review loop. Inspect the final review and run the build/test commands again.

Success means the new greeting behavior has executable tests, those tests pass,
and Humanize's review reports completion. A final assistant message alone does not
prove that the review hook ran.

## Desktop app alternative

Use the app's **local Codex coding task** attached to this folder. The exact product
label can be Codex or the Codex area of the ChatGPT desktop app. This workflow needs
local file access, command execution, and the Humanize lifecycle hook; an ordinary
chat without that local project context is not sufficient.

1. Complete the local installation, CLI login, and hook trust steps above.
2. Add/open this `kda` folder as a local project
   in the desktop app. Start a fresh task using the project directory directly.
   For this first run, avoid a separate worktree: ignored local skills and hook
   files are not part of a Git checkout, and the Windows runtime uses absolute paths.
3. Check that the Humanize skills are available. In ChatGPT's skill picker, type
   `@` and select the skill; Codex CLI uses `$` or `/skills`. If the app exposes a
   Codex-style skill picker, use that picker to attach the installed skill.
4. Invoke `humanize-gen-plan` with
   `--input docs/draft.md --output docs/plan.md`. Inspect and commit the plan.
5. Invoke `humanize-rlcr` with
   `docs/plan.md --track-plan-file --max 3`. Keep all rounds in that task.

The separate reviewer still needs the working local Codex CLI installation and
login. If the app does not expose or run the required hook, use the CLI for a fresh
loop; do not treat an unreviewed app run as equivalent. The customized skill binds
an active loop to its implementing session, so do not switch clients mid-loop.

The CLI recommendation is based on making the Humanize hook setup easy to inspect;
the full Humanize loop has not been run as part of creating this scaffold.

## What each file does

- `src/main.c`: default and personalized greetings, with argument-count validation.
- `tests/check_output.cmake`: runs the application and checks its output and status.
- `AGENTS.md`: shared coding rules and build/test commands.
- `docs/draft.md`: first task brief. Humanize generates `docs/plan.md` later.
- `.gitignore`: excludes builds, local skills, hook configuration, and loop state.

After this exercise, add a small computation with reference test vectors, then a
board target and repeatable target measurements. Host tests establish correctness;
host timings alone do not establish embedded performance. An embedded knowledge
skill can be installed when it becomes useful.

## References

- `.agents/skills/humanize/SKILL.md`: installed runtime and workflow instructions.
- `.agents/skills/humanize-gen-plan/SKILL.md`: plan generation instructions.
- `.agents/skills/humanize-rlcr/SKILL.md`: implementation and review instructions.
- [Official skill discovery and invocation](https://learn.chatgpt.com/docs/build-skills)
- [Official hook discovery and trust](https://learn.chatgpt.com/docs/hooks)

The installed skills document the Windows runtime and session binding. The official
documentation describes the host's skills and hooks. An end-to-end Humanize run
has not been performed as part of creating this scaffold.
