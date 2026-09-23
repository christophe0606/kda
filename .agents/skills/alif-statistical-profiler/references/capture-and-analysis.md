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

## Export through native CMSIS MCP memory reads (verified)

After capture has completed, pause HP through MCP. Confirm the HP symbol context
and evaluate `statistical_samples.header`. The demo continues updating globals
after capture stops: use the header's saved iterations/validation result for the
capture, not a later reading of the live counters.

Evaluate `&statistical_samples` and `sizeof(statistical_samples)` to get the
current address and length; do not hardcode an address from a previous build.
Require complete=1 and active=0, then use `read_memory` in contiguous chunks of
at most 4096 bytes while the target stays halted. For the default buffer this is
16 chunks. Validate every returned address and byte count before concatenating
the bytes; reject partial reads or gaps. This method worked with Developer
Assistant 2.3.9 and CMSIS Debugger 1.8.0.

If saving the parsed bytes as a plain hexadecimal string, convert them offline
with `bytes.fromhex(text)` in Python or `[Convert]::FromHexString(text)` in modern
PowerShell, and write binary bytes to `build/profile/hp_samples.bin`. Include the
whole object, including unused slots. Pass that binary file to the decoder below.
The first verified hardware capture used this method after `-exec` was rejected.

## Optional GDB console export

The bundled script defines an export command and checks completion before dumping
the whole fixed-size object. Use these GDB commands only through the existing
CMSIS debug session, never a standalone shell GDB process:

```gdb
source .agents/skills/alif-statistical-profiler/scripts/export_profiler_buffer.gdb
export_profiler_buffer build/profile/hp_samples.bin
```

Use the debug adapter's documented command prefix when sending these through
a console. Do not assume ordinary C-expression evaluation supports GDB commands:
this environment's MCP `evaluate_expression` rejected `-exec source ...`.
Prefer the native memory-read export above rather than retrying console escapes.
If needed, the user can run the script in VS Code's existing HP Debug Console
using the command syntax shown there. Use absolute forward-slash paths
if that session's working directory differs from the workspace; quote paths with
spaces as needed. Inspect an export error rather than repeatedly relaunching.

The capture must remain halted during the dump. `active == 0` by itself is
insufficient: `complete` must be 1. `sampling_profiler_stop` performs finalization
and cache maintenance. Do not dump a live buffer or edit its flags to bypass the
guard. A fresh capture requires a deliberate new lifecycle in firmware or a new
verified launch; the demo does not automatically overwrite its first capture.

## Obtain load evidence when MCP only reports dispatch

For the verified run, the existing `CMSIS Load` task temporarily received
`-O` and `logging=build/profile/load-logging.yaml` as two additional arguments.
The bundled loader supports this logging configuration (no separate loader is
launched). Create the output directory first and use, for example:

```yaml
version: 1
disable_existing_loggers: false
handlers:
  evidence:
    class: logging.FileHandler
    filename: build/profile/load.log
    mode: w
root:
  level: INFO
  handlers: [evidence]
```

Launch only through CMSIS MCP. The log must name both selected Release HEX paths
and show completed programming without errors. Preserve it with the matching
artifacts. Restore the task's arguments afterwards so a temporary file is not a
permanent launch dependency. Programming statistics do not establish byte-for-byte
readback verification. Preserve the normal launch/reset workaround in AGENTS.md.

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

For pictures, offline HTML and Perfetto output, see [display-results.md](display-results.md).

Firmware layers remain external at `../../cmsis_statistical_profiler`.
`scripts/analyze_profiler_buffer.py` and `scripts/export_profiler_buffer.gdb` were
copied unchanged from that checkout's `host/` and `tools/` directories at commit
`ad5342b73780d60bcb379caac0c6a6125663ec67`. The upstream Apache-2.0 LICENSE is included.
The upstream visualizer and its requirements are also vendored; see the display
reference. Firmware sources are not copied.

The current format uses a 164-byte header and 24 + 4 * active-PMU-count bytes per
record. The upstream prototype may change its format/APIs. When updating the
external checkout, compare its `FORMAT.md`, configuration/API and both scripts;
refresh the bundled helpers and rerun checks together. A format version alone
does not guarantee compatibility with a changing prototype.
