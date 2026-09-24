# Helium f32 block FIR optimization plan

## Goal Description

Implement an application-local, single-precision block FIR optimized for the Alif E8 M55_HP using Helium, with CMSIS-style processing arguments, a candidate-specific `kda_fir_instance_f32`, and implementation-defined state. The instance-pointer type may differ; source/destination/block-size arguments, return type, public coefficient ordering and mathematical FIR behavior retain the agreed contract. Coefficient padding, internal packing/storage, state size/layout, initialization, alignment and other implementation-specific restrictions must be chosen for the independent candidate, not inherited from CMSIS-DSP. Compile measured kernels with `-O3 -ffast-math`, support tiny and odd dimensions, and demonstrate per-case performance at least as fast as the pinned CMSIS-DSP 1.18.0 Helium baseline.

The user resolved the asymptotic interpretation: `0.621 * blockSize * numTaps` is a target in cycles, not an invented hard tolerance. The 0.621 target assumes all FIR computation data in DTCM and executed FIR code in ITCM, as clarified by the user. Report normalized cycles, gap and scaling from B>=128 and N>=16 under verified placement. Cached external data or code outside ITCM may legitimately deviate; do not treat such a deviation as an algorithmic failure or compare those results directly with this target. CMSIS parity remains mandatory. Planning adds no application code or benchmark results.

Repository baseline: `ac5e3409e68a58862fccf2cde68f7d57990ee8c3`. The HP application currently runs two statistical-profile workloads; HE idles. Host CMake tests cover the greeting. Release already selects speed optimization and fast math; old generated commands show `-O3`, but each new measured build must be verified again.

## Acceptance Criteria

The common correctness and timing matrix is the Cartesian product:
- B (block samples): {1,2,3,4,5,7,8,15,16,17,31,32,63,64,127,128,129,256,512}.
- N (taps): {1,2,3,4,5,6,7,8,9,15,16,17,31,32,33,64,128}.

These are mandatory test points, not algorithmic maximum sizes. Valid callers supply positive representable dimensions and sufficiently sized buffers; allocation/index arithmetic must not overflow. No runtime argument-checking wrapper or new error return is required for the void CMSIS-style API. Zero sizes, null pointers and insufficient allocations are invalid. Document and validate candidate-specific aliasing and block-size-change rules; do not infer them from the comparator.

- AC-1: Preserve CMSIS-style processing arguments, public coefficient ordering and FIR behavior while allowing a custom candidate instance and initialization-time precomputation.
  - Use `void kda_fir_f32(const kda_fir_instance_f32 *S, const float32_t *pSrc, float32_t *pDst, uint32_t blockSize)`. Define an application-local `kda_fir_instance_f32` with independently chosen fields, including pointers/metadata for reordered or packed coefficients when beneficial. Only the instance type changes from the agreed CMSIS-style call; remaining parameter types/order and void return remain. Keep the stock `arm_fir_instance_f32` untouched for the comparator. No struct layout/ABI compatibility, pointer casts between instance types, or state/initializer interchangeability is required.
  - Define the mathematical filter independently as `y[n] = sum(k=0..N-1, b[k]*x[n-k])`. The coefficient array accepted by the candidate's public initialization API must use the CMSIS public order `{b[N-1], ..., b[1], b[0]}`; b[0] multiplies the current sample x[n]. Caller-visible reordering is not an implementation choice. Initialization may populate additional instance fields and produce reordered/packed coefficient storage for processing. This must preserve the public interpretation and leave the caller's supplied coefficients unchanged. Document field meanings, derived-buffer size/alignment and ownership/lifetime, whether original coefficients remain referenced or are fully copied, and how reinitialization rebuilds every dependent field when coefficients or dimensions change. Processing consumes the prepared instance; callers never supply the private coefficient order. Document streaming history and internal mapping. Internal coefficient packing, any coefficient padding, state layout/size, scratch, alignment, ownership/lifetime, initialization/reset and fixed-versus-variable block-size support are candidate design choices, not inherited CMSIS constraints. Use custom initialization/allocation helpers where needed, while retaining the processing contract above, including its approved custom instance type.
  - Zero-padding coefficients is not required by this plan. A candidate may use exactly N coefficients, or require additional padding only if its independently developed design justifies and documents it. Apply the same rule to alignment, divisibility, buffer aliasing and storage restrictions: impose only requirements justified by the candidate, without importing them from the comparator. Every mandated small/odd case must remain supported. All allocation/index arithmetic must be overflow-safe; per-instance mutable data must not couple independent instances.
  - Positive Tests: Compile candidate calls against its public `kda_fir_instance_f32` declaration and CMSIS calls against the stock declaration without casts. Allocate and initialize candidate and CMSIS fixtures independently according to their own contracts, with identical logical coefficients in the same CMSIS public order and zero-history starting conditions. Process at least eight consecutive identical blocks per matrix point and compare each output stream with the independent oracle. Include a non-symmetric impulse case: mathematical taps b={1,2,3}, supplied array {3,2,1}, expected outputs {1,2,3} from zero history, including across block boundaries. Test initialization-time conversion using non-symmetric coefficients and any chosen private layout; reinitialize with different coefficients and verify that stale precomputed fields cannot affect outputs. Check continuity, reset and instance independence through outputs; do not compare raw state or transfer it between implementations. Exercise exact N-element coefficient buffers when that is the chosen candidate contract; otherwise validate only the padding explicitly justified by that candidate.
  - Negative Tests: Reverse the required public coefficient order incorrectly, corrupt a derived coefficient field/mapping, retain stale precomputed data after reinitialization, or corrupt a history update, or reset history between blocks; the oracle must detect output errors. Fixture validation rejects allocations below the candidate's documented requirement and other invalid inputs before calling an unchecked void API. It must not reject valid unpadded coefficients or odd sizes merely because the CMSIS fixture has different requirements.
- AC-2: Produce numerically correct f32 outputs under the required optimization.
  - Use a separate double-precision direct-convolution oracle compiled without fast math/reassociation. For each output calculate Q=sum(abs(b[k]*x[n-k])) in double, u=2^-24, gamma=(2*N*u)/(1-2*N*u). Require `abs(actual-reference) <= 1e-7 + gamma*Q` on the declared finite-input suite. Report maximum absolute/scaled error and apply the same check independently to CMSIS. Do not widen tolerances after observing candidate failures.
  - Finite normal-range input/coefficient vectors are required; generated values and sums avoid overflow and subnormal-sensitive cases. NaN/Inf payloads, signed-zero identity, subnormal preservation, and bit identity are not promised with fast math. Record FPSCR/flush settings for reproducibility.
  - Positive Tests: Impulse, zeros, constant/ramp, non-symmetric coefficients, alternating signs/cancellation, and reproducible random streams; include bounded mixed magnitudes. Validate actual target Release outputs, not just a host surrogate. Verify reinitialization resets a stream and two instances remain independent.
  - Negative Tests: Inject an output error larger than its computed bound, NaN in output for finite expected data, a dropped sample, and a wrong coefficient order; the checker must fail. Keep finiteness/error checks in a strict-math test translation unit or host reader so fast math cannot optimize them away.

- AC-3: Handle every tiny/odd matrix point with safe buffer accesses.
  - Candidate initialization and processing accesses stay within documented instance, source, destination, public/derived coefficient and state/scratch allocations. Source and coefficients remain unchanged for distinct-buffer calls. Any padding or temporary storage must be explicitly justified by the independently designed candidate and included in its allocation contract. No implicit CMSIS-derived padding is allowed.
  - Positive Tests: Mandatory candidate-only source and final-disassembly access proof for every dispatch, copy and tail path, bounding all reads/writes by the exact allocations and valid positive dimensions. Record maximum offsets and predicates, including unrolled iterations, and re-audit changed assembly. Use target MPU guard tests as the selected dynamic mechanism: in a separate safety-test mode, place each buffer in turn with its exact end against a no-access boundary, respecting MPU region granularity, and record fault status outside timing. Combine this with write canaries and boundary placements satisfying the candidate's own documented alignment; vary valid alignments where supported. Never inspect comparator instructions to establish candidate safety. Exercise B/N 1..4, each dispatch boundary, and vector tails. Establish MPU configuration/support in T1/T5 rather than assuming instruction tracing is available. If dynamic read protection is unavailable, record the safety result as unqualified and AC-3 incomplete; a static proof alone must not silently pass. Host sanitizers on a scalar oracle alone cannot establish MVE safety.
  - Negative Tests: Run separate negative-control safety images through the same serialized CMSIS workflow: a volatile unpredicated tail read across the MPU boundary and a one-float boundary overwrite must produce the expected fault, while all valid candidate cases complete. Record known fault/recovery separately from benchmark health. Canaries alone do not prove read safety.
  - Treat CMSIS-DSP as an opaque comparator. Configure its fixture only from public API/usage documentation and supported build metadata; baseline-only documented allocations or padding apply solely to that fixture. Do not derive requirements by reading source, disassembly, internal comments, traces or past source-analysis notes. Use separate allocations and setup for each implementation, matched memory class/common valid alignment, equivalent logical coefficients/input streams, and deterministic initialization. Report both footprints. If a baseline requirement or failing case cannot be resolved from permitted information and black-box output/timing observations, report it as unqualified; do not reverse-engineer or silently claim parity.
- AC-4: Build and link the intended Helium implementation reproducibly.
  - Keep AC6 6.24.0, Cortex-M55, CMSIS-DSP 1.18.0, and matched Release HP/HE contexts. Inspect effective compile commands for candidate and baseline `-O3 -ffast-math`; establish MVE float enablement and absence of `ARM_MATH_AUTOVECTORIZE`. Inspect only candidate disassembly and harness call sites to prove the candidate general path executes Helium f32 arithmetic. Establish baseline identity and MVE selection through public build configuration, compiler command metadata, component/version identity and linked symbol names; do not inspect its function bodies or disassembly. Small cases may use specialized scalar paths.
  - Positive Tests: Validate csolution/cproject before cbuild; CMSIS MCP build exits successfully; map contains distinct candidate/reference symbols with known build provenance and no unintended replacement; candidate-only disassembly shows MVE instructions on its timed path. Keep debug symbols for evidence.
  - Negative Tests: Verification rejects missing flags, a comparator whose supported MVE build provenance cannot be established without implementation inspection, duplicate symbols, wrong build context, or merely existing/stale artifacts without successful build completion.

- AC-5: Obtain reproducible, fair whole-function PMU measurements with explicit code/data placement.
  - Primary memory profile: execute the timed FIR code, its callees and measurement call path from ITCM; place all data accessed by the timed FIR computation in DTCM, including input/output, original coefficients if read, precomputed coefficient buffers, instance fields, history/scratch, stack and accessed constant tables. Apply the same residency conditions to candidate and comparator. Initialization-only or unrelated application storage need not occupy TCM if it is not accessed during the timed computation.
  - Verify execution addresses (not merely image/load addresses), linker map/section placement, relevant runtime data/stack addresses and available ITCM/DTCM capacity using public build metadata and supported MCP observations. Candidate-only inspection remains allowed; do not inspect comparator implementation to establish placement. Route its code/data through supported linking/allocation facilities. Any spill, relocation or fallback outside the required regions disqualifies that case from the TCM profile; report it rather than silently treating cache hits as DTCM/ITCM. Unproven residency is unqualified for the target, not proof of a performance miss.
  - Optional external-memory, mixed-placement or non-ITCM-code experiments are separate profiles. Record memory regions, cache enablement and warm/cold/cache-maintenance policy and use matching conditions for candidate versus CMSIS. Warm caches do not turn external memory into TCM. Do not pool these samples with TCM captures or fit a single asymptotic model across placements; these optional runs do not substitute for the primary target evaluation.
  - Use CMSIS Core PMU APIs such as `ARM_PMU_Get_CCNTR`, not DWT, host wall time, or sample percentages as performance evidence. Confirm counter availability, cycle prescaler/filter settings and wrap handling. Disable statistical sampling and other avoidable interrupts during timed batches, preserve/restore interrupt state, and record conditions. HE remains idle. No UART, allocation, initialization, verification or debugger halt in the timed region.
  - Measure the whole public processing call including dispatch, scratch work and every required state update, regardless of representation. Candidate and stock CMSIS use the same firmware, clocks, compiler options, memory class/alignment, input stream, warm-up, call mechanism, and PMU setup. Keep candidate/reference in separate translation units with LTO disabled for the benchmark and use equivalent out-of-line call sites; inspect harness/candidate call sites and symbol metadata for distinct calls and no elimination/inlining artifacts, without opening comparator function bodies. Keep outputs observable. Each implementation uses its own initialization outside steady-state processing timing with equivalent zero-history conditions; candidate coefficient reordering and derived-field preparation occur during initialization, not as hidden untimed per-block work; repeated calls maintain equivalent deterministic streams. Different allocation sizes, coefficient padding/private packing, state layouts and initialization are allowed and must be reported. Supply coefficients in the same public order to both implementations; use separate setup only for their documented allocation/padding and private packing needs, use common valid alignment and the same memory class, and record each contract without imposing comparator requirements on the candidate. Processing current input and recurring state maintenance belong inside the timed call, not in untimed setup.
  - Use at least 31 paired batches per case after warm-up, alternate order, and calibrate repetitions so timing overhead is under 1% or explicitly flag a case as unresolved. Bound each interval below a counter wrap and reject overflow/invalid reads. Capture raw cycles, repetitions, empty-harness overhead, median per-call cycles, min/max and dispersion. Define C as the median of raw batch PMU deltas divided by repetitions: this inclusive whole-call metric retains public-call overhead plus the reported amortized harness overhead. Use only C for parity and, for verified ITCM-code/DTCM-data captures, the 0.621 report. Report empty-harness-corrected statistics separately as diagnostics; never use subtraction to meet acceptance. Record exact deterministic vector seeds/ranges, source identity, compiler/flags, FPSCR, HP/HE image hashes, memory-placement profile, code execution/data address ranges, cache configuration and warm/cold policy where applicable, and each implementation's coefficient/state footprint, padding/alignment, coefficient mapping and initializer identity in capture metadata.
  - Positive Tests: Empty-harness calibration and a known repeated workload establish a live PMU; two independent full captures use identical protocol and remain stable (batch median absolute deviation <=1% of median per case). Retain both runs; instability blocks performance conclusions and triggers investigation.
  - Negative Tests: Reject false TCM labels, unknown residency used to claim TCM performance, pooled results from different memory profiles, frozen/wrapped counters, mismatched placement/flags, timed logging, active sampling interrupts, missing raw evidence, or results taken from the wrong loaded image.

- AC-6: Meet CMSIS parity and report the asymptotic target honestly.
  - Every mandatory matrix case must pass correctness and have candidate median cycles <= CMSIS median cycles in each of the two qualified primary TCM captures. Candidate/CMSIS parity always compares like-for-like memory placement; the exemption from the 0.621 target outside TCM is not a waiver for unfair comparison or hidden regressions. Do not trade a tiny-case regression for a large-case gain or use aggregate speedup to hide failures. A noisy, missing or invalid case is unresolved, not a pass. No unapproved parity allowance is introduced.
  - For all B>=128,N>=16 matrix points in the verified ITCM-code/DTCM-data profile, publish r=C/(B*N), signed gap r-0.621, signed percentage gap `100*(r/0.621-1)`, speedup `C_CMSIS/C_candidate`, and row/column trends. For each qualified TCM capture independently, fit an auxiliary `C=a*B*N+b*B+c*N+d` model by unweighted ordinary least squares over every point in that region, reporting coefficients, per-point residuals, residual RMS and rank/conditioning checks; do not filter out inconvenient points. The coefficient a estimates asymptotic cycles per product; the measured full-call values remain primary. Highlight onset points B=128,N=16 and neighbors 127/129 and 15/17 rather than cherry-picking large cases.
  - Positive Tests: Per-case report passes matched-placement parity; the TCM scaling plot/table includes the entire asymptotic subset and reports approach to, or remaining gap from, 0.621. A remaining TCM gap is investigated and reported honestly. For optional non-TCM profiles, report measured normalized cycles/trends and matched CMSIS speedup separately; mark the 0.621 target comparison as not applicable to those conditions. External-memory/cache or non-ITCM-code overhead alone is not evidence of a bad FIR algorithm.
  - Negative Tests: Report checker fails a planted per-case regression, omitted mandatory TCM matrix cell, cross-placement speedup comparison, or non-TCM result labeled as a 0.621-target failure. Inner-loop-only cycles, fitted extrapolation alone, or one favorable large point cannot establish the requested full-function behavior.

- AC-7: Develop independently, preserve application boundaries, and record every candidate and performance commit.
  - Design from the FIR equation, permitted Arm/Helium instruction and compiler documentation, original candidate reasoning, and candidate measurements. No agent or reviewer may inspect CMSIS-DSP implementation source, inline/private implementation code, disassembly, internal algorithm descriptions, instruction traces, or derivative source-analysis artifacts. Do not search elsewhere for copies, ask another agent to inspect them, copy/adapt known CMSIS internals, or call CMSIS as a candidate fast path/fallback. Public declarations and standalone usage documentation may be consulted only for ABI and correct black-box baseline invocation. Build tools may compile/link the stock dependency opaquely; that is not authorization to read it.
  - Record each candidate's design rationale and permitted evidence sources. The original planning session inspected CMSIS code before this prohibition; those observations must not guide design. Historical review artifacts remain quarantined evidence, not implementation references. Do not assert a clean-room history or unique machine code; independently derive and justify the design, and report any further prohibited exposure honestly.
  - Keep the host greeting and six CTest cases, HE idle role, and original statistical demo available. Add an explicit local FIR test/benchmark mode inside the Release target instead of silently swapping target sets. Raw outputs belong in `runs/`, `outputs/`, or `profile/`. Keep external Humanize/profiler checkouts and installed packs untouched.
  - `solutions.jsonl` records every implementation candidate, including rejected/unmeasured ones, unique ID, parent IDs, source identity, rationale and evidence/status. Preserve existing records and validate an acyclic graph with all parents present. Planning candidate documents are Humanize planning evidence, not measured FIR implementations.
  - Create `benchmark.csv` with candidate/commit or source hash, case, build/tool/pack identity, cycles/statistics, correctness, and evidence path. Record every performance-related commit, explicitly using not_measured when needed rather than inventing cycles; update identity after commit without requiring a self-referential commit hash. Link each measured case to raw captures and HP/HE image hashes.
  - Positive Tests: Candidate provenance records identify permitted derivations; candidate call graph/symbol references contain no CMSIS FIR calls, and reviewer prompts carry the same no-inspection constraint. Existing CTest suite passes; original demo remains selectable; bookkeeping validator verifies DAG and every reported performance commit/case has evidence or explicit unmeasured status.
  - Negative Tests: Reject CMSIS-derived candidate designs, comparator implementation inspection tasks, hidden candidate calls to CMSIS, inherited restrictions without candidate justification, missing parents, cycles, orphan benchmark rows, fabricated measurements, or passing summaries with unmeasured cases. Final diff contains no dependency/skill/runtime edits.

## Path Boundaries

### Upper Bound (Maximum Acceptable Scope)

An application-local Helium FIR with a custom instance type and implementation-defined state, initialization-time coefficient preprocessing and any needed allocation helpers, limited small-tap specializations, safe tails, strict correctness oracle, target PMU benchmark, reproducible report/check scripts, and documentation/ledger integration. Changes may touch `src/`, `tests/`, a new local `scripts/` directory, `CMakeLists.txt`, `kda.cproject.yml`, narrowly necessary `kda.csolution.yml` build options, `README.md`, `docs/`, `benchmark.csv`, and `solutions.jsonl`. Preserve the launch workaround; linker/board changes are permitted only if demonstrated necessary for fair placement or safety testing and recorded explicitly.

### Lower Bound (Minimum Acceptable Scope)

One correct Helium general kernel plus only the specializations required to pass all matrix cases, the agreed processing argument/return contract with its custom instance pointer, documented candidate instance/coefficient/state contracts and initialization, correct streaming behavior and independent-design provenance, independent numerical and memory checks, both qualified ITCM-code/DTCM-data hardware captures, matched-placement per-case parity, a TCM asymptotic report, and complete candidate history. A host-only implementation, benchmark scaffold without board evidence, scalar CMSIS comparison, or unresolved parity failure is incomplete.

### Allowed Choices

- Can use: C11, MVE intrinsics, small measured assembly helpers if justified, output tiling/unrolling, scalar tiny-filter paths and predicated tails, a custom `kda_fir_instance_f32` with additional initialization-derived fields, independently justified coefficient/state layouts, sizes, optional padding and initialization/allocation helpers, application-local build adapters, uv for Python tooling.
- Cannot use: CMSIS-DSP implementation inspection, copying/adapting its algorithms or code, candidate dispatch to a CMSIS routine, changes to processing arguments beyond the approved candidate instance type, changed public coefficient ordering or FIR output semantics, FFT FIR substitution, pack edits, external Humanize/profiler changes, pyOCD installation, direct pyOCD/GDB board access, WSL, a replacement review loop, or profiling percentages as cycle measurements.
- Preserve `.vscode/launch.json`: Release program, `run: "all"`, no `tbreak main`, manual configuration. Use native Windows tooling without the VS Code enabled environment.

## Feasibility Hints and Suggestions

These suggestions do not prescribe a kernel before measurement.

### Conceptual Approach

Derive candidate algorithms from the FIR equation and the target's published Helium capabilities. Evaluate original data organization and vectorization alternatives, explaining memory traffic, dependency chains, register demand, arithmetic work and total per-call overhead using candidate-only reasoning and measurements. Beyond the required public coefficient order, no particular tiling, dispatch threshold, coefficient padding, internal storage format or initialization algorithm is prescribed or to be taken from CMSIS. Choose the custom instance fields and initialization-time coefficient transformation independently; no field names/layout or internal ordering are prescribed. Choose an implementation-specific contract that supports every required small/odd test case, document it, and build the oracle and safety tests from that contract.

Retain the distinct `kda_fir_f32` name and stock comparator symbol so both can be linked for measurement. Treat dependency build composition as opaque: use supported project component settings, compile-command metadata and symbol maps to establish linkage without opening implementation files. Inspect only candidate code/disassembly and harness calls. CMSIS comparisons supply correctness/output and elapsed-cycle observations, not algorithm instructions or instruction traces.
Keep statistical capture separate from this small-kernel benchmark. If later investigating a large application hotspot, use the installed statistical profiler skill and exact-image decoding; it is not required to locate this already-known kernel.

### Relevant References

- `prompts/fir.md`: original draft, unchanged; later resolved user clarifications below take precedence.
- Public CMSIS processing declarations and standalone baseline usage/build documentation: ABI and opaque comparator setup only. Do not open implementation source or implementation-oriented documentation.
- Published Arm Helium/instruction-set and compiler documentation, FIR mathematics, candidate-only source/disassembly, and PMU captures: permitted design and optimization evidence.
- `src/main.c`, `src/kda_profiler_config.h`: existing HP demo and sampling configuration.
- `kda.csolution.yml`, `kda.cproject.yml`, `.cmsis/tools-environment.yml`: targets, dependencies, compiler and tooling.
- `CMakeLists.txt`, `tests/check_output.cmake`, `M55_HE/main.c`: regression boundaries.
- `solutions.jsonl`: preserve existing candidate lineage; current entries are unmeasured.
- `C:/ctools_pack/ARM/CMSIS/6.3.0/CMSIS/Core/Include/m-profile/armv8m_pmu.h`: existing PMU access helpers.

## Dependencies and Sequence

### Milestones

1. Contract and baseline: apply resolved DEC-2/DEC-3/DEC-4/DEC-5/DEC-6/DEC-7; derive and document the candidate contract from FIR mathematics and permitted architecture references, establish the opaque baseline fixture from public usage documentation, and verify build provenance without inspecting its implementation; establish ITCM/DTCM placement and capacity for the timed code/data before benchmarking; establish strict oracle, case manifest and independent safety mechanism before optimizing.
2. Correct candidate and target harness: implement the custom instance, initialization-derived coefficient fields, processing with the chosen representation, allocation helpers and benchmark mode; keep host/demo behavior selectable; validate solution configuration before any CMSIS build.
3. Hardware qualification and optimization: build through CMSIS MCP, verify loaded images, run correctness/safety, verify execution/data residency, qualify PMU, then iterate measured kernels under the TCM profile. Do not time known-incorrect candidates as successful results.
4. Final evidence: repeat qualified TCM captures, enforce matched-placement per-case parity, publish TCM asymptotic gap/trends and separately labeled optional memory profiles, run regressions and verify records.

Hardware workflow for implementation: serialize all top-level build/load/run/debug operations. Query session status, stop any old debug session or CMSIS Run task, and verify termination before another probe-owning action. A timeout is not completion. After a successful validated Release build, follow `kda.cbuild-idx.yml` to both selected contexts and `out/kda+DevKit-E8.cbuild-run.yml`. Check target-set and resolved image/symbol paths for `M55_HP` and `M55_HE`: `out/kda/DevKit-E8/Release/kda.{hex,axf}` and `out/M55_HE/DevKit-E8/Release/M55_HE.{hex,axf}`, with correct image/symbol load roles. Confirm CMSIS Load/preLaunchTask selects this same configuration.

Launch through `cmsis_action(action="load_and_debug", target="DevKit-E8@Release", timeoutMs=60000)`, then poll status without relaunching. Obtain successful load output naming both images; verify target/symbols and completed benchmark/correctness results with an advancing progress marker. A responsive DAP alone proves neither programming nor application health. This repository has no renderer/hyperbolic service; FIR progress/results are the applicable runtime evidence. If MCP cannot establish completion or probe termination, request the user's VS Code intervention and label the blocked evidence incomplete. Avoid load_and_run, direct probe tools, and timeout-prone continue/reset workarounds.

## Task Breakdown

Each task has exactly one routing tag. `coding` is implementation in the future active session; `analyze` uses the installed independent `scripts/ask-codex.sh`, not a custom review loop. Every implementation/reviewer prompt must include the no-CMSIS-implementation-inspection rule and only permitted context. Do not attach or consult quarantined old analysis/candidates to design the filter.

| Task ID | Description | Target AC | Tag (`coding`/`analyze`) | Depends On |
|---------|-------------|-----------|------------------------|------------|
| T1 | Freeze candidate contract, opaque baseline, safety protocol and feasible ITCM/DTCM benchmark placement | AC-1, AC-3, AC-4, AC-7 | analyze | - |
| T2 | Add case manifest, strict oracle, streaming/reference and negative-control tests | AC-1, AC-2, AC-3 | coding | T1 |
| T3 | Implement custom FIR instance, initialization/precomputation helpers, independently designed processing and integration preserving other modes | AC-1, AC-3, AC-4, AC-7 | coding | T2 |
| T4 | Add PMU harness, verified memory-profile metadata/capture reporting and bookkeeping validation | AC-5, AC-7 | coding | T1, T2 |
| T5 | Validate/build/load through CMSIS, establish safety/correctness, code/data residency and TCM baseline captures | AC-1, AC-2, AC-3, AC-4, AC-5 | coding | T3, T4 |
| T6 | Review candidate-only assembly, design provenance, black-box baseline fairness and measured bottlenecks | AC-4, AC-5, AC-6 | analyze | T5 |
| T7 | Optimize and register each candidate, including rejected variants; repeat correctness | AC-1, AC-2, AC-3, AC-6, AC-7 | coding | T6 |
| T8 | Produce two final TCM captures, matched-placement parity/asymptotic report and regression evidence | AC-5, AC-6, AC-7 | coding | T7 |
| T9 | Independently review permitted evidence, design provenance, processing API and per-case acceptance | AC-1, AC-2, AC-3, AC-4, AC-5, AC-6, AC-7 | analyze | T8 |

## Codex-Codex Deliberation

- Planner: GPT-6-based Codex session; exact deployed model identifier unavailable.
- Historical reviewer: `gpt-5.6-luna`, effort `high`, independent CLI process; first-pass analysis and two discussion rounds succeeded on the original version.
- Historical evidence directories: `.humanize/skill/2026-09-24_07-41-12-88-90a18224/`, `.humanize/skill/2026-09-24_07-46-38-419-29957643/`, `.humanize/skill/2026-09-24_07-50-08-681-40369cfd/`, and `.humanize/planning/fir-20260924-01/`. Each reviewer directory contains input/output/metadata. These artifacts include prior implementation-derived context: retain for provenance only; do not read/reuse them for future design or give them to implementation/review agents.
- Previous state-contract refinement: `.humanize/plan_qa/plan-qa.md` (historical).
- Current refinement QA: `.humanize/plan_qa/tcm-placement/plan-qa.md`. Previous custom-instance refinement: `.humanize/plan_qa/custom-instance/plan-qa.md`. Previous independent-design/order refinement: `.humanize/plan_qa/independent-design/plan-qa.md`. No new independent review was performed, as prescribed by the refinement workflow.

### Agreements

Earlier discussion agreed on processing-signature compatibility, output correctness, candidate memory safety, fair PMU measurements, mandatory CMSIS parity and asymptotic reporting. User clarifications DEC-3/DEC-4/DEC-5/DEC-6/DEC-7 now supersede all inherited implementation restrictions and authorize only independent candidate design with an opaque comparator. Earlier review is not represented as agreement with these subsequent revisions.

### Resolved Disagreements

| Topic | Planner position/history | Reviewer position/history | Current resolution |
|---|---|---|---|
| Target and function name | Asked user to resolve material interpretation | Requested explicit target/API scope | DEC-1 sets reported asymptotic target with parity; DEC-2 approves distinct symbol with matching processing signature |
| Candidate state/init | Earlier plan imposed comparator compatibility | Earlier review accepted that assumption | DEC-3 removes state/init compatibility; candidate contract is independently designed |
| Coefficients and other implementation constraints | Earlier plan retained inherited padding/restrictions | No new review of this clarification | DEC-4 removes inherited constraints; any candidate restriction requires its own design justification |
| Public coefficient ordering | Latest refinement briefly allowed caller-visible ordering changes | No new review | DEC-5 restores the required CMSIS public coefficient order while allowing private packing |
| Custom instance and precomputation | Earlier plan retained the stock instance type | No new independent review | DEC-6 permits `kda_fir_instance_f32` and initialization-derived fields; public coefficient order remains fixed |
| Memory conditions for 0.621 | Earlier plan omitted the target's memory-placement condition | No new independent review | DEC-7 requires verified ITCM code/DTCM data for this target; other profiles are reported separately |
| Implementation inspection | Original planning inspected comparator code | Historical evidence includes implementation-derived analysis | Prohibited from now on, including through agents or derivative notes; prior exposure is disclosed and not a design input |
| Safety and performance protocol | Earlier draft had incomplete test/metric definitions | Requested executable safety checks and reproducible reporting | Candidate-only safety audit/guards and raw inclusive PMU metrics retained; baseline qualification now uses only public interface/build information and black-box runs |

### Convergence Status

- Final Status: `converged` for resolving the user-directed refinements.
- Independent discussion rounds: 2 on the original plan; 0 new rounds during the subsequent refinements.
- Pending user decisions: none. Refinement resolution does not imply new independent reviewer agreement, clean-room provenance, completed implementation or achieved performance.

## Pending User Decisions

None remain. Resolved decisions are retained below for traceability.

- DEC-1: Interpretation of 0.621.
  - Planner Position: Report target gap and onset under the ITCM-code/DTCM-data conditions of DEC-7; require matched-placement CMSIS parity.
  - Reviewer Position: Distinguish aspirational target from hard threshold explicitly.
  - Tradeoff Summary: Quantified reporting without an arbitrary percentage bound.
  - Decision Status: User chose reported asymptotic target and mandatory CMSIS parity.
- DEC-2: Public symbol spelling.
  - Planner Position: Retain `kda_fir_f32` with the agreed CMSIS-style processing arguments for same-image comparison; DEC-3 governs state/init freedom and DEC-6 permits the custom instance pointer type.
  - Reviewer Position: Make the distinct-name versus replacement-symbol choice explicit; recommends distinct name unless a true replacement is required.
  - Tradeoff Summary: Distinct name avoids the stock aggregate's duplicate symbol; exact replacement needs build isolation.
  - Decision Status: Distinct name remains approved; the earlier state/init interpretation is superseded by DEC-3.

- DEC-3: Scope of CMSIS API compatibility (supersedes the state/init portion of DEC-2).
  - Planner Position: Match the processing contract as amended by DEC-6; allow implementation-defined state content, size and organization with documented allocation and custom initialization. Check equivalent outputs, not state interchangeability.
  - Reviewer Position: Earlier review assumed shared CMSIS state/init semantics. This user-directed refinement has not received a new independent review.
  - Tradeoff Summary: State representation can suit the optimized filter; callers use its documented allocation/reset/initialization procedure. Benchmarks use separate correctly initialized states.
  - Decision Status: Resolved by the user's explicit clarification on 2026-09-24; no pending decision.

- DEC-4: Independently derived implementation contracts and prohibition on CMSIS-DSP implementation inspection.
  - Planner Position: Keep the processing argument/return contract with the custom instance allowed by DEC-6, public coefficient order (DEC-5) and FIR output semantics. Derive private coefficient packing/padding, state and other constraints independently; compare to CMSIS as an opaque baseline.
  - Reviewer Position: No fresh review; prior implementation-derived findings are superseded and excluded from future design context.
  - Tradeoff Summary: Public API/usage/build information and black-box outputs/timings are allowed. Source, internal algorithms, disassembly, instruction traces and derivative descriptions are forbidden for all agents/reviewers. Earlier source exposure is recorded, not erased or claimed to be clean-room work.
  - Decision Status: Resolved by the user's explicit direction on 2026-09-24. Coefficient zero-padding is optional only when independently justified by the candidate; no CMSIS implementation constraint applies automatically.

- DEC-5: Preserve CMSIS public coefficient ordering.
  - Planner Position: The supplied coefficient array uses `{b[N-1], ..., b[0]}` for `y[n]=sum(b[k]*x[n-k])`, matching the public processing contract. Private packing may differ without changing caller-visible ordering or output semantics.
  - Reviewer Position: No new independent review; this is a direct user correction to the scope of DEC-4.
  - Tradeoff Summary: Coefficient interpretation is fixed; optional padding and internal/state organization remain independently justified implementation choices. No comparator implementation inspection is needed.
  - Decision Status: Resolved by the user's explicit correction on 2026-09-24. Supersedes any earlier wording permitting a different public coefficient order.

- DEC-6: Custom instance type with initialization-derived coefficient fields.
  - Planner Position: Use `kda_fir_instance_f32` where additional fields support reordered/packed coefficients or other independently designed preparation. Initialization accepts the existing public coefficient order and computes those fields. Processing keeps the same source/destination/block-size arguments and void return, with the candidate instance pointer type.
  - Reviewer Position: No new independent review; prior assumptions requiring the stock instance type are superseded by this direct user clarification.
  - Tradeoff Summary: Internal fields and ordering can support performance without caller-side reordering. Document allocation, ownership/lifetime and rebuilding derived data on reinitialization. The candidate and CMSIS instances are distinct types, not ABI-interchangeable objects.
  - Decision Status: Resolved by the user's explicit clarification on 2026-09-24. Supersedes the stock-instance-type restriction while preserving DEC-5 and the no-implementation-inspection rule.

- DEC-7: Memory-placement conditions for the asymptotic target.
  - Planner Position: Evaluate 0.621 cycles per sample-tap product only with verified FIR code in ITCM and all FIR computation data in DTCM. Record placement/capacity evidence and keep other memory profiles separate.
  - Reviewer Position: No new independent review; the target's conditions are supplied directly by the user.
  - Tradeoff Summary: Cache/external-memory accesses or code outside ITCM can legitimately add cycles. They do not by themselves invalidate the algorithm; compare candidate and CMSIS under matching conditions and do not infer a TCM target miss from non-TCM measurements.
  - Decision Status: Resolved by the user's clarification on 2026-09-24. Refines DEC-1 without introducing a hard tolerance or inspecting CMSIS implementation details.

## Implementation Notes

### Code Style Requirements

Implementation code and comments must not contain plan-specific markers such as AC-, Milestone, Step or Phase. Use domain names. Do not adapt CMSIS-DSP routines. Attribute permitted public mathematics/architecture references without importing comparator implementation details. Tests should establish observable behavior, buffer safety and measurement validity rather than mirror kernel internals.

No RLCR loop was requested or started. Later implementation must use the installed skill and its generated review instructions. This plan does not certify hardware feasibility or performance before measurement.

## Output File Convention

Output: `docs/plan.md`, originally generated after independent analysis/discussion and now refined from the user clarification. Current refinement QA: `.humanize/plan_qa/tcm-placement/plan-qa.md`; custom-instance QA remains `.humanize/plan_qa/custom-instance/plan-qa.md`; independent-design/order QA remains `.humanize/plan_qa/independent-design/plan-qa.md`; prior state refinement QA remains `.humanize/plan_qa/plan-qa.md`. Earlier candidate/review records remain unchanged, quarantined historical evidence, not implementation references. Preserve `prompts/fir.md`.

### Translated Language Variant

Effective `ALTERNATIVE_PLAN_LANGUAGE` is empty; no translated variant is required.