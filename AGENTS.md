# Project instructions

This is a portable C application for exercising the existing Humanize planning and
implementation/review skills. Run commands from this repository root.

## Build and test

```text
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Rules

- Use portable C11 and the standard library. Do not allocate memory dynamically.
- Keep the project small. No board SDK, RTOS, extra test framework, or custom agent
  orchestrator is needed for the initial task.
- Keep output deterministic. Treat user strings as data, never as format strings.
- Run the build and tests after code changes. Report actual results and any checks
  that could not run.
- Keep generated build files in `build/`.
- Limit changes to this application. The surrounding Humanize checkout is an
  external dependency, not implementation scope.

## Humanize workflow

- The user installs and invokes the existing Humanize skills separately.
- `docs/draft.md` is the first task brief; `docs/plan.md` is the generated plan.
- During plan generation, write the plan without implementing the requested change.
- During an active loop, follow the installed Humanize skill and its generated
  round instructions. Do not replace its review hook with a custom loop.
- Keep Humanize state and machine-local skill installations out of Git.
- No embedded knowledge-base skill is included in this starter.
