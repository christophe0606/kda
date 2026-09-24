# Independent f32 FIR candidate

The current candidate is a **host-tested scalar correctness foundation**, not the
finished Helium implementation. Target correctness, target memory guards, ITCM/DTCM
residency, PMU cycles and CMSIS parity are still unqualified. The existing board
demo and host greeting are unchanged.

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
| History | 2*N floats; retained for the instance lifetime |
| Mutable `kda_fir_state_f32` | History pointer and next write index; retained |
| `kda_fir_instance_f32` | Tap count, prepared pointer and mutable state pointer |

No coefficient padding or vector alignment is required. Arrays require their
natural float alignment; objects require their C type's alignment. All storage
objects and arrays are mutually disjoint. Source/destination overlap, including
in-place calls, is unsupported. Neither calls sharing mutable state nor concurrent
reinitialization/processing of one instance are supported. Separate instances can
share the original read-only public coefficients during initialization.

Initialization returns 1 on success, copies/reorders coefficients, clears all
history, sets the next index to zero and publishes every instance/state field.
Null pointers, zero taps or insufficient capacities return 0 **before any writes**,
leaving the instance, state, prepared buffer and history unchanged. Disjoint valid
objects are a caller precondition, not a runtime overlap-detection feature.
Successful reinitialization rebuilds all derived fields and history. Original
coefficients may be released or changed afterward; processing uses the prepared
copy. Call initialization again to change the filter, never patch its public input
array and expect a running instance to change.

`kda_fir_reset_f32` requires a valid instance, clears all 2*N history elements and
sets the next index to zero while retaining prepared coefficients. Processing
requires a valid instance and positive block size. It supports varying positive
block sizes between calls without reinitialization. `pSrc` and `pDst` each cover
the supplied block size and are disjoint from instance-owned storage. No function
allocates memory internally.

The f32 typedef is the standard C `float`; the C11 interface can coexist with an
identical public typedef without importing a DSP implementation header. The
candidate and CMSIS instance structs are not cast-compatible.

## Derivation and access bounds

The design comes from the FIR equation and periodic indexing, not from comparator
internals. Initialization prepares `prepared[k] = b[k]`. Each incoming sample is
written at ring position p and at p+N in a duplicated history array. The write
position moves backward modulo N. Consequently `history[p+k]` is the sample with
delay k, and one contiguous dot product computes the next output. There are two
state stores per input and no block-end history copy. The mutable index lives in a
separate state object so processing never casts away the instance's constness.

For `0 <= p < N` and `0 <= k < N`, the dot reads at most index `2*N-2`; the mirrored
store writes at most `2*N-1`. Prepared coefficient accesses are exactly `[0,N)`.
Only the requested source/destination samples are accessed. This is the scalar
source-level bound; future candidate machine-code and target MPU checks remain
necessary. Host canaries detect writes, not otherwise valid reads into a canary.

Future Helium versions must preserve this candidate's documented storage contract
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
is `0x95e1a123 ^ N ^ (B << 16) ^ pattern`.

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

## Evidence and independent-design boundary

Candidate IDs and parents are recorded in `solutions.jsonl`. `benchmark.csv` uses
source SHA-256 identities and a `Benchmark-Revision` commit trailer to identify
unmeasured performance-related commits without a circular self-hash. Resolve a
revision with `git log --all --grep='Benchmark-Revision: <revision>'`; measured
captures will additionally name exact commits/images. Blank cycle fields mean
not measured, not zero cycles.

CMSIS is an opaque future comparator. No implementation source, internal
descriptions, disassembly, instruction traces or old source-analysis notes may be
used by any implementer/reviewer. Permitted evidence is FIR mathematics, public
architecture/compiler documentation, application-local code/linker configuration,
candidate-only inspection and black-box comparator observations. Earlier planning
exposure is already disclosed in the plan; it is not a design reference.

Final target evaluation requires a validated CMSIS build/load, confirmed ITCM code
and DTCM computation data, target numerical/MPU checks, and two qualified PMU
captures. The 0.621 target does not apply to other memory-placement profiles.
