# Independent f32 FIR candidate

The current candidate uses independently derived **sixteen-output Helium tiles**
over a linear sample window, with shift-carry tiny-tap paths. It replaces the mirrored
ring after qualified measurements identified per-output history/reduction costs.
Its correctness, safety and performance must be requalified after this change.
The host greeting and
statistical board demo remain selectable; Release currently selects FIR correctness.

## Public contract

`src/kda_fir_f32.h` declares `kda_fir_instance_f32`, its mutable state object and:

```c
void kda_fir_f32(const kda_fir_instance_f32 *S,
                 const float32_t *pSrc, float32_t *pDst, uint32_t blockSize);
```

The public initializer accepts coefficients in `{b[N-1], ..., b[0]}` order for
`y[n] = sum(b[k] * x[n-k])`. It accepts explicit capacities in floats for the
public coefficients, prepared coefficients and history. `N` is a positive
`uint16_t`. The count helpers return zero for invalid/unrepresentable sizes and
widen arithmetic to `size_t` before computing byte bounds.

For this candidate, caller-owned storage is:

| Object | Size / lifetime |
|---|---|
| Public coefficients | Exactly N floats; only needed during initialization |
| Prepared coefficients | N floats; retained for the instance lifetime |
| History/work window | N+127 floats; retained for the instance lifetime |
| Mutable `kda_fir_state_f32` | Window pointer and history-start offset next in [0,128]; retained |
| `kda_fir_instance_f32` | Tap count, prepared pointer and mutable state pointer |

No coefficient padding or vector alignment is required. Arrays require their
natural float alignment; objects require their C type's alignment. All storage
objects and arrays are mutually disjoint. Source/destination overlap, including
in-place calls, is unsupported. Neither calls sharing mutable state nor concurrent
reinitialization/processing of one instance are supported. Separate instances can
share the original read-only public coefficients during initialization.

Initialization returns 1 on success, copies coefficients in public order, clears all
history, sets the next index to zero and publishes every instance/state field.
Null pointers, zero taps or insufficient capacities return 0 **before any writes**,
leaving the instance, state, prepared buffer and history unchanged. Disjoint valid
objects are a caller precondition, not a runtime overlap-detection feature.
Successful reinitialization rebuilds all derived fields and history. Original
coefficients may be released or changed afterward; processing uses the prepared
copy. Call initialization again to change the filter, never patch its public input
array and expect a running instance to change.

`kda_fir_reset_f32` requires a valid instance, clears all N+127 window elements and
sets the next index to zero while retaining prepared coefficients. Processing
requires a valid instance and positive block size. It supports varying positive
block sizes between calls without reinitialization. `pSrc` and `pDst` each cover
the supplied block size and are disjoint from instance-owned storage. B floats
must form a representable object/byte span (`B <= SIZE_MAX / sizeof(float32_t)`),
as well as fitting the actual supplied allocations. No function
allocates memory internally.

The f32 typedef is the standard C `float`; the C11 interface can coexist with an
identical public typedef without importing a DSP implementation header. The
candidate and CMSIS instance structs are not cast-compatible.

## Derivation and access bounds

The design comes from the FIR equation and measured candidate costs. Prepared
coefficients retain `{b[N-1],...,b[0]}`. For N>32, H=N-1 samples starting at
state.next hold oldest-to-newest history. Each chunk appends L<=128 inputs.
If next+L>128, an ascending overlap-safe copy first moves those H samples to
offset zero. Otherwise no compaction is needed. Output i is
`sum(prepared[k]*window[next+i+k])`; next then advances by L. The largest active
window index is next+H+L-1<=N+126. All append/compaction work is timed.
Sixteen outputs use four accumulators sharing each coefficient. Owned hardware
loops interleave contiguous vector loads and arithmetic; an eight-output tile
and predicated four-lane tail cover the remainder. Blocks shorter than four use
tap-wise vector dots. No gather-load latency assumption is used.
Full tiles initialize from the first product, accumulate the N-2 middle taps,
and interleave final-tap arithmetic with stores. This changes scheduling without
changing the logical sample/coefficient ranges or allocation contract.

N=5..8 retains H=N-1 oldest-to-newest samples at window[0..H). It copies only
E=min(B,round_up(H,4)) initial input samples after that history and computes the
first E outputs there. Remaining outputs use the source directly, beginning at
source[E-H]. Straight-line taps alternate contiguous loads and arithmetic in
tail-predicated four-output loops. The last H source samples become the next
history; when B<H, an ascending overlap-safe copy retains window[B..B+H).
This bounds boundary preparation to eight input samples per call, with all work
inside processing. The existing valid-buffer byte-span precondition is exposed
to the MVE compiler to prevent a hypothetical loop-index wrap and enable hardware
tail loops. N=2..4 also has a separate constant-tap helper for each count.
These specializations do not change storage or initialization; next stays zero
for N<=32.

N=9..32 uses the same boundary/direct-input split, with E<=32, and the existing
sixteen/eight-output tiles followed by predicated four-output tails. Its tap loop
retains a runtime count to avoid spilling a large set of hoisted coefficients.
Boundary append and final retention are bounded tail-predicated copies; short
blocks retain history with an ascending overlap-safe copy. The full N+127
allocation remains sufficient since the largest boundary index is N+30.

N=1 scales directly. N=2..4 retains up to three most-recent samples at window
indices0..2 and uses vector shift-with-carry, followed by a scalar remainder;
next remains zero on these tiny paths. Initialization/reset zero the entire
documented window, including workspace; no coefficient padding is introduced.
Block-size changes are supported because chunking occurs inside processing.
The non-MVE host path computes the same linear-window convolution scalarly.
Host canaries detect writes, not otherwise valid reads into a canary.

Future versions must preserve this candidate's documented storage contract
or explicitly create and document a new candidate contract. They must not silently
introduce padding, aliasing or alignment requirements.

## Host validation

Configure CMake using a host C compiler, then build and run CTest:

```powershell
cmake -S . -B runs/fir-host -DCMAKE_BUILD_TYPE=Release
cmake --build runs/fir-host --config Release
ctest --test-dir runs/fir-host -C Release --output-on-failure
```

`fir_correctness` checks the plan's 323 block/tap pairs, seven deterministic input
patterns, and eight blocks per pair/pattern, plus maximum uint16 tap count, variable
block sizes, independent instances, reset, changed coefficients/dimensions and
failure-atomic initialization. Patterns include a non-symmetric impulse, zero,
constant, ramp, cancellation, seeded random and bounded mixed magnitudes. The seed
is `0x95e1a123 ^ N ^ (B << 16) ^ pattern`. Random signed integers in [-1024,1024]
are divided by 1009, exercising non-exact floating-point products and sums.

`tests/fir_oracle.c` directly convolves the chronological stream in double
precision. It does not reproduce ring indexing. Its fixed per-output tolerance is
`1e-7 + gamma(2*N) * sum(abs(products))`, where `u=2^-24` and
`gamma(2*N)=2*N*u/(1-2*N*u)`. The checker and oracle are compiled without fast math;
the candidate is compiled with `-O3 -ffast-math` on Clang/GCC (MSVC uses
`/O2 /fp:fast`). Negative controls must be rejected for wrong coefficient order,
stale/corrupt prepared coefficients, lost history, dropped outputs, NaN and an
injected output error. The existing six greeting tests remain enabled.

Passing this host suite is not evidence of target performance or MVE safety. No
host timing is recorded as a function benchmark.

## Release target correctness mode

Set `KDA_APP_FIR` in `src/fir_profile.h` to 1 for FIR correctness or 0 for the
original statistical demo. Both use `DevKit-E8@Release` and the idle HE image.
The FIR mode does not initialize or enable statistical sampling. This is a
correctness image, not a benchmark capture. The selected scatter file currently
executes code from MRAM; it is explicitly ineligible for the 0.621 TCM comparison.

`src/fir_target.c` runs the shared 323-case matrix with seven patterns and eight
blocks against the strict double oracle, independently for candidate and opaque
CMSIS. It then runs candidate lifecycle/negative controls and baseline reset,
changed coefficients and interleaved-instance checks. Input/coefficient immutability,
missing outputs, absolute/scaled errors and FPSCR are checked or recorded.
`kda_fir_result` exposes progress and the first matrix failure to the debugger;
phase 3 means finished, and success additionally requires 2261 cases and zero
failures. `mpu_type` is capability metadata, not an MPU protection test.

Candidate and baseline calls have separate instances, prepared coefficients,
state and output buffers. The correctness workspace reserves arrays for maximum
matrix dimensions; result footprint fields report the per-case logical requirements,
not this backing allocation. Natural float alignment remains the candidate contract;
the static matrix workspace uses common 16-byte alignment. This workspace does not
replace exact-allocation MPU tests.

The opaque baseline follows only the versioned [CMSIS-DSP 1.18.0 public FIR usage
documentation](https://arm-software.github.io/CMSIS-DSP/v1.18.0/group__FIR.html):
`4*ceil(N/4)` coefficient floats with zero-valued padding, `N+2*B-1` state floats,
and `arm_fir_init_f32` with a fixed B per initialized fixture. The public
[library overview](https://arm-software.github.io/CMSIS-DSP/v1.18.0/index.html)
also requires three readable words beyond vector buffers. Baseline arrays reserve
that additional margin, including separate source storage; the baseline footprint
fields include it for coefficients/state. These requirements
are exclusive to the baseline. Both initializers receive the same public order.
Public headers are compiled opaquely; implementation source is never opened.

Release candidate and DSP commands use AC6 6.24, Cortex-M55, `-O3 -ffast-math`
and `-fno-lto`. Checker/oracle commands override fast math with `-fno-fast-math
-ffp-contract=off`; compile-time assertions reject an incorrect checker configuration,
missing candidate MVE float support or a non-MVE/autovectorized comparator selection.
An exact `.bss.dtcm.fir` selector prevents collision with SRAM `.bss.*` selectors.

CMSIS Load retains its output in `runs/cmsis-load.log` through the existing CMSIS
task. Its preparation dependency creates `runs/` on a fresh checkout. CMSIS task
regeneration can replace these custom task settings; check them after conversion
and restore log capture before loading. Inspect the log
for both selected image paths and completed programming; a responsive debugger or
MCP success message alone is insufficient. A failed link can leave an older ELF.

Round 1 target evidence (`runs/fir-reference/target-result.json`) records 2261/2261
matrix cases and zero failures after lifecycle/control tests. Maximum absolute
errors were 1.5187543e-6 for the candidate and 2.9796502e-6 for CMSIS; maximum
error/bound ratios were 0.205222 and 0.209653. FPSCR was `0x00040010` before and
`0x80040010` afterward; MPU TYPE was `0x00001000`. These are numerical results
from the MRAM correctness image, not safety, residency or performance acceptance.

Candidate-only final disassembly shows `dlstp.32` using N, two `vldrw` streams,
`vfma.f32`, and `letp` before scalar reduction, with mirrored scalar stores.
The emitted tail-predicated loop supports the source bound above. The complete
candidate access audit and guarded target results are in [fir-access.md](fir-access.md).
Initial numerical evidence:
`runs/fir-reference/candidate-final-mve.txt`, `selected-commands.json`,
`baseline-commands.json`, `image-hashes.txt`, and `final-load.log`.

Use `KDA_APP_FIR=2` for the healthy MPU suite, `3` for the deliberate vector-read
fault, or `4` for the deliberate scalar-write fault. Each requires a separate
validated Release build and serialized CMSIS load. Modes 3/4 intentionally halt
in HardFault; let the handler run past debugger vector catch to retain its record.
Restore mode 1 and reload after controls. The healthy suite passed 6460 cases;
both controls faulted at `0x20001500` with CFSR `0x82` and HFSR `0x40000000`.
Artifacts under `runs/fir-guard/{healthy,read-fault,write-fault}` retain image hashes,
build/load logs and raw/decoded MPU results. These MRAM-code tests qualify the
audited candidate buffer accesses, not TCM performance. Re-audit and rerun guards
after changes that alter candidate instructions.

## Reproducible TCM and PMU profiles

`uv run --no-project --python 3.13 scripts/build_fir.py numerical --tcm --output runs/my-numerical`
selects the local profile, validates the solution, builds Release and archives its
inputs. Modes are `demo`, `numerical`, `guard`, `read-fault`, `write-fault` and
`benchmark`. Omit `--tcm` for the separate MRAM-code profile. The selected header
is the sole source of mode/memory macros; it is archived with source hashes,
compiler commands, generated contexts/run configuration, expanded scatter file,
map, section flags and both core images. Existing evidence directories cannot be
overwritten. Inputs changing during validation/build invalidate the archive.
Serialize this script with CMSIS build/load/debug operations. It never accesses
the board; load through CMSIS MCP and retain the fresh load log beside each capture.

The TCM scatter profile keeps startup/root sections in MRAM, moves other code to
ITCM and read-only data to the existing DTCM region. FIR and comparator translation
units use `-mexecute-only`; ELF `SHF_ARM_PURECODE` section flags establish that the
timed code contains no literal data without opening comparator instructions.
Startup/profiler assembly is excluded from that option. Check symbol addresses,
linker cross-reference identities, capacities, section flags and live data/stack
addresses for each image; a requested TCM profile alone does not qualify it.

Mode 5 first runs the full strict numerical suite. It then checks PMU presence,
live cycle progression, disabled-counter behavior and a deliberately induced
overflow, preserving the cycle filter. Timestamp reads use CMSIS Core CCNTR APIs.
On this target, `CTRL.CYCCNT_DISABLE` stops CCNTR and the cycle bit in CNTENSET
enables its overflow reporting; both controls are exercised by the probes.
It retains raw inclusive deltas for 31 alternating paired batches per matrix case,
with common calibrated repetitions, 128 warmup calls and separate fixtures.
Sampling is not started; interrupts are masked during measurement and restored
afterward. Initialization, strict periodic-stream checks and metadata are outside
the timed whole-call intervals. Candidate/reference batch call sites reside in
separate translation units with LTO disabled and identical calling structure.

Export `kda_fir_benchmark` through supported MCP memory reads only after completion.
Save `memory.json` with the starting `address` and complete byte array `bytes`.
`scripts/report_fir.py --profile runs/my-build --capture runs/my-capture` checks
metadata and reports medians, ranges, MAD and overhead diagnostics. Inclusive C
is raw batch median divided by repetitions. Each loop now contains 128 direct
public calls, with assembly-identical candidate, comparator and ABI-empty call
sites. A separate control uses the same prologue, epilogue and outer loop but
omits the public calls. Its maximum raw cost bounds added harness overhead;
the ABI-empty measurement separately includes required public-call cost.
Cases with added harness overhead above 1%, or MAD above 1%, remain
unresolved; repetitions cannot amortize per-call loop costs. An observed cycle
comparison is not a qualified parity claim until every required gate passes.

The initial TCM profile `runs/fir-tcm/benchmark-v4` has two complete independent
captures in `capture-4` and `capture-5`. Both report 323 cases, no target failures,
no provenance-check errors and no cases exceeding 1% batch MAD. Both remain
**unqualified**: empty-call-loop overhead exceeds 1% in 237 cases. The raw inclusive
comparison shows the candidate slower in 282 cases in each capture. These are
diagnostics for further harness work and optimization, not accepted parity or
asymptotic results. Accepted cycle fields in `benchmark.csv` remain blank.

The user subsequently authorized measurement-overhead subtraction where needed
for small-cycle accuracy. Raw inclusive values remain primary when the added
harness cost already meets the 1% gate. The report retains paired loop-control
subtractions as diagnostics; accepting a corrected metric additionally requires
calibration-sensitivity and noise evidence. It never subtracts FIR processing,
dispatch, public call/return, argument setup or recurring state maintenance.

New profiles archive an exact committed input tree plus an explicit generated
mode overlay in `runs/fir-generated/`; diagnostic dirty builds are rejected by
the acceptance checker. The runtime record carries the recipe fingerprint.
Supported MCP readback must match both complete programmed images and all
relocated timed code bytes, compared opaquely without instruction decoding.
The report checks the export address/size, linker cross-reference closure,
full code/data spans and observed stack watermark with MSPLIM bounds. This
qualifies the executed matrix, not every possible input or an unknown indirect
path. `compare_fir.py` enforces matching protocols/images/repetitions and <=1%
between-capture drift for every case.

Earlier harness attempts are retained: v1 overflowed the stack while zeroing a
large volatile aggregate; v2 failed the stopped-counter probe; v3 failed the
overflow-reporting probe. The v4 fixes passed both PMU probes and eight planted
invalid-evidence controls. The FIR algorithm was unchanged across these attempts.
After captures, `runs/fir-tcm/numerical-final` restored numerical mode with TCM
enabled: validated Release build, completed dual-image programming, 2261 numerical
cases with zero failures, no live fault flags, and debugger detached.

## Evidence and independent-design boundary

Candidate IDs and parents are recorded in `solutions.jsonl`. `benchmark.csv` uses
source SHA-256 identities and a `Benchmark-Revision` commit trailer to identify
unmeasured performance-related commits without a circular self-hash. Resolve a
revision with `git log --all --grep='Benchmark-Revision: <revision>'`; measured
captures will additionally name exact commits/images. Blank cycle fields mean
not measured, not zero cycles.

CMSIS is an opaque comparator. No implementation source, internal
descriptions, disassembly, instruction traces or old source-analysis notes may be
used by any implementer/reviewer. Permitted evidence is FIR mathematics, public
architecture/compiler documentation, application-local code/linker configuration,
candidate-only inspection and black-box comparator observations. Earlier planning
exposure is already disclosed in the plan; it is not a design reference.

Final target evaluation requires a validated CMSIS build/load, confirmed ITCM code
and DTCM computation data, target numerical/MPU checks, and two qualified PMU
captures. The 0.621 target does not apply to other memory-placement profiles.
