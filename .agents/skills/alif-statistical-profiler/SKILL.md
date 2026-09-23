---
name: alif-statistical-profiler
description: Profile CPU hotspots on the KDA Alif E8 board using timer-based statistical PC sampling into RAM, then export and decode the capture against its exact AXF. Use for on-board profiling, finding expensive functions, or guiding benchmark optimization; use cycle or elapsed-time measurements separately for precise performance comparisons.
---

# Alif E8 statistical profiling

Use sampling to discover where a sustained CPU workload spends time without
instrumenting each function. It is useful before optimization and alongside
benchmarks. It estimates exclusive PC-hit shares, not call counts, call trees,
worst-case latency, exact cycle shares, or accelerator utilization. For short
kernels, repeat representative inputs and measure correctness and elapsed cycles
separately. For faults or hangs, use the live debugger instead.

## KDA integration

Run host commands from the KDA repository root. Read `kda.cproject.yml`,
`kda.csolution.yml`, `src/main.c`, and `src/kda_profiler_config.h` first.
The cproject references the external `../../cmsis_statistical_profiler` checkout:
common capture layer, Alif E8 adapter, and UTIMER layer. Do not copy or edit those
firmware sources to tune this application. If the checkout moves, update all
three references to its actual location.

Current configuration:

- HP only, Secure privileged Cortex-M55; HP board layer selects `trustzone: secure`
  so AC6 supplies `-mcmse`. HE remains an idle companion image.
- UTIMER channel 0, overflow `UTIMER_IRQ7Handler` / IRQ 384, 400 MHz timer input
  under the default E8 clock setup. HP enables its shared clock bit before init.
  Neither HE nor another driver may use that channel or vector.
- 1000 samples/s, 65536-byte buffer, PMU disabled. Buffer section
  `.bss.dtcm.profiler` goes to HP-local DTCM; stack bounds use SDK DTCM bounds.
- Release stays `optimize: balanced`; debug information is enabled for both
  images so types and symbols remain available. Do not profile an unoptimized
  Debug build when assessing Release performance.
- `workload_light` and `workload_heavy` repeatedly increment volatile globals
  2000 and 6000 times. They are non-inlined and called forever. The first capture
  stops when full or after three CPU seconds, then the workload continues.
  UART output is outside capture. Expect approximately 25%/75% within these two
  functions, with overhead and sampling aliasing; do not enforce exact shares.

## Capture a workload

1. Select representative inputs and check numerical/output correctness. Warm up
   outside the recording window when measuring steady-state performance. Preserve
   compiler settings, clocks, memory/cache placement, inputs and iteration counts
   across comparisons. Prevent dead-code elimination using observable outputs;
   the synthetic demo deliberately uses volatile counters.
2. Call lifecycle APIs serially in privileged HP thread mode:
   `sampling_profiler_init()` (check success), `sampling_profiler_enable()`, run
   the workload, then `sampling_profiler_stop(iterations, validation_passed)`.
   Init starts the timer with recording gated off. Always stop, even if full;
   disabling alone does not finalize a capture. Do not print, halt, sleep, or add
   breakpoints inside the measured window. Put any completion breakpoint after stop.
3. Validate the CMSIS solution before building: use Toolbox
   `csolution update-rte kda.csolution.yml --active DevKit-E8@Release` with schema
   checking enabled. If unavailable, ask the user to confirm no validation errors
   in VS Code. Build through `cmsis_action(action="build", target="DevKit-E8@Release",
   timeoutMs=60000)` and require successful task completion. A timeout is not a
   failed or stopped task; establish completion in VS Code before another operation.
4. Follow this repository's AGENTS.md flash workflow. Stop and verify termination
   of any prior debug/Run session. Inspect `kda.cbuild-idx.yml`, both selected
   cbuild files, and `out/kda+DevKit-E8.cbuild-run.yml`. Require matching HP and HE
   Release image and symbol paths. Preserve the manual HP launch's `run: "all"`,
   absence of `tbreak main`, and `cmsis.updateConfiguration: "manual"`.
   Use CMSIS MCP `load_and_debug`; require actual load completion evidence for
   both image paths. A responsive debugger proves attachment, not programming.
5. Let the capture finish without halting. Then use CMSIS MCP to pause and confirm
   target/core/AXF with `get_device_info`. Inspect
   `statistical_samples.header`: require `complete == 1`, `active == 0`,
   `count > 0`, and successful workload validation. Check `rejected` and each
   rejection reason. Init failure leaves an incomplete capture. A zero-sample
   timeout is an investigation result, not a successful profile.
6. Export and analyze using [capture-and-analysis.md](references/capture-and-analysis.md).
   Preserve the exact unstripped AXF before rebuilding. Inspect unknown PCs,
   rejected frames, buffer saturation and timing validity before interpreting
   percentages. Report sample count, workload, build/settings, and limitations.

Interact with the board only through CMSIS Developer Assistant MCP. Never install
or directly launch pyOCD/GDB, start a second probe server, or use a raw GDB remote
connection. If MCP cannot export through the existing session, have the user run
the bundled commands in VS Code's active debug console. Repeated MCP failures
require VS Code intervention; do not work around them with a separate debugger.

## Tune and interpret

Use `PROFILER_USER_CONFIG` / `src/kda_profiler_config.h` for project-wide settings.
The common layer already defines rate and capacity; use `#undef` then `#define`
in this header to override them without macro redefinition ambiguity. Defaults
hold 2723 PC-only records, roughly 2.723 seconds at 1000 Hz. Full means a prefix
was captured; increasing the buffer or lowering the rate can cover a longer phase.
Change rate (for example 997 Hz) and repeat to detect aliasing with periodic work.
The demo's three-second timeout is also a limit; adjust it deliberately if needed.

Keep the actual UTIMER input clock and `SystemCoreClock` accurate and fixed.
The configured 400 MHz follows the pack's default setup, not a runtime clock
measurement. Verify clock/security routing if startup changes. DWT wraps after
about 10.74 seconds at 400 MHz; avoid gaps that make wrap reconstruction ambiguous.
Long interrupt masking, higher-priority handlers, sleep, and sampling overhead
bias results. The backend rejects unsupported frames: it does not provide a
complete interrupt-time profile. LR is only the interrupted link register.

Optional PMU events need exclusive counter ownership; set `PROFILER_PMU_COUNT`
to 1-4 only when useful and inspect actual availability/status and overflow flags.
Each event consumes two chained 16-bit counters and adds four bytes per record.
Totals cover init-to-stop execution, including interrupts and gated-off periods;
do not attribute an interval's events to the sampled function.

Use timings from a separate sampler-disabled run to validate performance gains.
Do not equate a changed PC-hit percentage with an absolute speedup.

For HE profiling, read the external adapter's README first. It needs its own
Secure build, independent instance and physically separate buffer, distinct timer
channel (default 1), and a serialized shared-clock setup/readiness handshake.
The current HP-only clock update is insufficient for concurrent initialization.
Stop both captures before halting either core; decode each with its own AXF.
Per-core DWT clocks have no shared epoch; do not combine percentages or timelines.

## Validation scope

This integration was validated by a successful CMSIS Release build, linker-map
inspection, host tests, and synthetic decoder checks. Hardware timer operation,
capture/export and the expected load split still require an on-board run.
The source and bundled-script provenance are in the analysis reference.
