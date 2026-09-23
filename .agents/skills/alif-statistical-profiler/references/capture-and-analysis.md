# Export and analyze

Paths below are relative to the KDA root. Keep generated captures and reports
under `build/profile/`. Create that directory and preserve the programmed AXF:

```powershell
New-Item -ItemType Directory -Force build/profile | Out-Null
Copy-Item out/kda/DevKit-E8/Release/kda.axf build/profile/hp.axf
Get-FileHash build/profile/hp.axf -Algorithm SHA256
```

Do this only after confirming which image was programmed, before any rebuild.
Record the selected build, source revision, clocks, rate, PMU settings, image
hashes, and workload with the capture. Hashing a local file alone does not prove
that it matches the board. Do not overwrite a previous capture unintentionally.

## Export from the existing CMSIS debug session

After capture has completed, pause HP through MCP. Confirm the HP symbol context
and evaluate `statistical_samples.header`. The demo continues updating globals
after capture stops: use the header's saved iterations/validation result for the
capture, not a later reading of the live counters.

The bundled script defines an export command and checks completion before dumping
the whole fixed-size object. Use these GDB commands only through the existing
CMSIS debug session, never a standalone shell GDB process:

```gdb
source .agents/skills/alif-statistical-profiler/scripts/export_profiler_buffer.gdb
export_profiler_buffer build/profile/hp_samples.bin
```

Where the connected CMSIS MCP `evaluate_expression` supports the adapter's
`-exec` console escape, submit `-exec source ...` and then
`-exec export_profiler_buffer ...`. Confirm the tool accepted the commands and
that the output file exists. Do not assume ordinary C-expression evaluation
supports GDB commands. If unsupported, ask the user to execute the two commands
with `-exec` in VS Code's active HP Debug Console. Use absolute forward-slash paths
if that session's working directory differs from the workspace; quote paths with
spaces as needed. Inspect an export error rather than repeatedly relaunching.

The capture must remain halted during the dump. `active == 0` by itself is
insufficient: `complete` must be 1. `sampling_profiler_stop` performs finalization
and cache maintenance. Do not dump a live buffer or edit its flags to bypass the
guard. A fresh capture requires a deliberate new lifecycle in firmware or a new
verified launch; the demo does not automatically overwrite its first capture.

## Offline decoding

The Python script uses only the standard library for this C project:

```powershell
uv run python .agents/skills/alif-statistical-profiler/scripts/analyze_profiler_buffer.py --samples build/profile/hp_samples.bin --elf build/profile/hp.axf --output build/profile/hp-report
```

Native Python can replace `uv run python`. No pyOCD, GDB or board connection is
needed for analysis. For C++ names, the optional `--cxxfilt` argument accepts an
existing demangler; `--no-demangle` disables that subprocess.

Read `summary.json` first: count/rejections, validation, `unknown_samples`,
`timing_valid`, requested/nominal sample rates, PMU status, and artifact hashes.
Then read `functions.csv` for exclusive hit counts/percentages and `samples.csv`
for the sequence. `events.csv` exists only when PMU data is available. A successful
decoder exit does not guarantee a useful measurement; warnings need examination.
The ELF identity is not embedded in the capture, so a plausible decode against
the wrong AXF can still be wrong.

For the demo, look for more `workload_heavy` hits than `workload_light`; the
nominal ratio is 3:1. Loop unrolling, memory costs, phase locking to the sample
period and other code affect observed shares. Repeat at a different sample rate
if the split is surprising. Never report synthetic decoder fixtures as board
measurements.

## Provenance and compatibility

Firmware layers remain external at `../../cmsis_statistical_profiler`.
`scripts/analyze_profiler_buffer.py` and `scripts/export_profiler_buffer.gdb` were
copied unchanged from that checkout's `host/` and `tools/` directories at commit
`ad5342b73780d60bcb379caac0c6a6125663ec67`. The upstream Apache-2.0 LICENSE is included.
Only these host-side helpers are vendored; firmware sources are not copied.

The current format uses a 164-byte header and 24 + 4 * active-PMU-count bytes per
record. The upstream prototype may change its format/APIs. When updating the
external checkout, compare its `FORMAT.md`, configuration/API and both scripts;
refresh the bundled helpers and rerun checks together. A format version alone
does not guarantee compatibility with a changing prototype.
