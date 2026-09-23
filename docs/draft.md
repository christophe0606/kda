# First task: a personalized greeting

## Purpose

Exercise the existing Humanize draft-to-plan and implementation/review workflow on
a small C application. Performance optimization and embedded hardware support are
later tasks.

## Starting point

The application prints `Hello, embedded!` followed by a newline and exits
successfully. CMake builds it, and CTest checks the default output.

## Requested change

Accept an optional name as a single command-line argument:

1. With no arguments, print exactly `Hello, embedded!` and a newline to standard
   output, produce no standard error, and return success.
2. With the single argument `Alice`, print exactly `Hello, Alice!` and a newline,
   produce no standard error, and return success. Use the same format for any
   single supplied name, including spaces when passed as one argument.
3. With more than one argument, print a short usage message to standard error,
   produce no standard output, and return a nonzero exit status.

## Constraints

- Portable C11 and the standard C library only.
- No dynamic allocation, fixed-size name buffer, or platform-specific API.
- Treat the name as data, including names containing percent characters.
- Keep the executable target named `hello` and retain the existing build commands.

## Acceptance evidence

The Release build succeeds. CTest verifies the default greeting, a supplied name,
and rejection of excess arguments, including output streams and exit status.
Also check a name containing a space and a percent character to verify that the
name is handled as data. Tests must run the built application.

## Out of scope

Board support, cross-compilation, timing benchmarks, optimization claims, new
dependencies, knowledge-base skills, and changes to Humanize itself.

## Planning output

Use the installed Humanize planning skill to turn this draft into `docs/plan.md`.
Inspect the generated plan before starting the installed Humanize implementation
and review loop. Do not implement this draft during plan generation.
