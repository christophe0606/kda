# Candidate buffer-access ledger

## Current linear-window candidate

Candidate `fir-tail16-v1` uses exactly N public/prepared coefficients and N+127
work-window floats. Instance/state ABI sizes remain12/8 bytes; state.next is the
history start in [0,128] for N>32, zero for N<=32. Buffers are disjoint and naturally aligned. B must form a valid
representable float object. No comparator instructions were inspected.

The source proof and candidate-only AC6 audit are tied to
`runs/fir-r4/direct8-bound-numerical/candidate-only-disassembly.txt`. This compile/audit
is not by itself a dynamic safety pass; fresh guard/control results are required.
The medium-tap helper and modified dispatcher are additionally audited in
`runs/fir-r4/direct32-loops-numerical/medium-disassembly.txt`.
The current seeded tiles, containing medium/general helpers and dispatcher,
are audited in `runs/fir-r4/seeded-numerical/changed-disassembly.txt`.
The short-block helper, tiny2/tiny3 and current dispatcher are audited in
`runs/fir-r4/paired-short-tail-numerical/changed-disassembly.txt`.
The current medium/window helpers and dispatcher are audited in
`runs/fir-r4/tail16-numerical/changed-disassembly.txt`.

| Path | Access bounds and emitted implementation |
|---|---|
| Counts/init validation | N>0 and N+127 floats fit size_t byte arithmetic. All capacities/nulls are checked before caller stores. |
| Coefficient copy | Reads C[0..N), writes P[0..N). Scalar or `dlstp.32` copy, tail count N; no unpredicated overread. |
| Init/reset clearing | Clears exactly 4*(N+127) bytes through the standard word-clear runtime helper; writes instance offsets0,4,8 and state offsets0,4. |
| N=1 | Reads/writes only B source/destination elements; scalar low-overhead loop or `dlstp.32` scale with B-count tail. No history access. |
| N=2..4 | Reads/writes only window[0..N-1), reads exactly N coefficients. Full four-sample vectors use shift-with-carry to insert streaming history; each carry becomes the last lane shifted out. One to three scalar remainder loads/stores stay within B. |
| N>8, B<8 | Uses fixed history for N<=32 and lazy offset/compaction for N>32. Appends B inputs with DLSTP/LETP. Paired outputs share a coefficient vector; the tap loop uses DLSTP/LETP with count N to predicate all three loads and both FMAs. Active k+j<N bounds coefficient reads and sample index i+k+j<=B+N-2. An odd output has one predicated dot product. Scalar reductions store exactly B outputs. N<=32 retains H samples with ascending predicated load/store vectors; each vector is loaded before storing, safe for overlapping spans. N>32 advances next by B. All accesses obey the existing N+127 allocation. |
| N=5..8 boundary preparation | H=N-1, E=min(B,round_up(H,4))<=8. Predicated copies read source[0..E) and write window[H..H+E). Coefficients[0..N) are loaded exactly. Boundary outputs read window[i+k+j] with active i+j<E and k<N; maximum H+E-1<=14, within N+127. N=5 uses explicit VCTP/VPST for its one boundary vector; N=6..8 use DLSTP/LETP with count E. |
| N=5..8 direct suffix | Exists only when B>E, so E>=H. Base is source+E-H, length B-E. DLSTP/LETP predicates all shifted loads and destination stores. Last active source index E-H+(B-E-1)+(N-1)=B-1; first index E-H>=0. Stores cover destination[E..B). The compiler assumption length<=SIZE_MAX/4 follows the public valid-float-object contract and prevents i+=4 from wrapping on the 32-bit target. |
| N=5..8 retention | For B>=H, scalar/LDM loads read source[B-H..B) and store window[0..H). For B<H, ascending scalar load/store pairs copy window[B..B+H) to window[0..H); B=E, so all reads are initialized history or freshly appended input. Source is ahead of destination, preserving overlap. next remains zero. |
| N=9..32 | Same direct-input proof, with E<=32 and maximum boundary index H+E-1<=N+30. Boundary/suffix full tiles use the eight/sixteen-output bounds below; four-output tails predicate every sample load/store. Coefficients are loaded only for k<N. Append and retention use DLSTP/LETP counts E and H; the forward overlapping retention loads each vector before storing it, so no later source element is overwritten. next remains zero. |
| Lazy compaction | Let s=next. If s+L>128, ascending copy reads window[s..s+H) and writes window[0..H), then sets s=0. Old s<=128, so maximum read is N+126. Source is ahead; every vector loads before its store. Predicated copy count H. |
| General input append | H=N-1; L=min(B_remaining,128). After compaction check s+L<=128. Reads source[0..L), writes window[s+H..s+H+L). Scalar or tail-predicated copy with count L. The following tile bounds are relative to window+s. |
| General L<4 | Each output i<L accumulates taps in four lanes, with `dlstp.32` count N predicating coefficient/sample reads. The final active sample index is i+N-1<=H+L-1. Scalar horizontal reduction writes one output. |
| Eight-output tile | Requires i+8<=L. Each k<N reads coefficients[k], window[i+k..i+k+3] and window[i+k+4..i+k+7]. Maximum active index i+N+6<=N+L-2=H+L-1<=N+126. Two vector stores cover destination[i..i+8). |
| Sixteen-output tile | Requires i+16<=L. Each k<N reads coefficients[k] and four contiguous vectors at i+k+{0,4,8,12}. Maximum relative index i+N+14<=N+L-2. Adding s stays <=N+126. Four stores write exactly destination[i..i+16). |
| Thirteen-to-fifteen-output tail | Requires 13<=R=L-i<=15. The first three sample vectors and stores cover exactly outputs 0..11. VCTP(R-12) and VPST predicate every fourth-vector load and its store. Its last active read is i+(N-1)+(R-1)=N+L-2; no padded source/coefficients are needed. N>8 makes the N-2 DLS/LE middle loop positive. P0 is saved before and restored after the asm because AC6 rejects VPR/P0 register-clobber spellings. Every VPT block is consumed within the asm, and all modified GPR/vector/condition/memory operands are declared. |
| Four-output tail | `vctp.32(L-i)` predicates each sample load and final output store. Each active lane j<L-i has i+k+j<=N+L-2. The scalar coefficient load remains bounded by k<N. |
| History retention | Advances s by L, maintaining 0<=s<=128. The H samples at this new offset are the latest history. Publishes s in state.next at return. No copy until space is needed. |
| Chunk advance | Advances source/destination by L and decreases positive remaining B by L. Chunk indices stay <=128; no B+N arithmetic is used. |

The public dispatcher tail-branches to scale, specialized tiny/fixed, or general
window helpers. Scale has an8-byte frame; tiny2/3/4 have16/24/32-byte frames;
fixed5/6/7/8 have32/48/48/56-byte frames; medium has80 bytes and general window
has104 bytes; short-block has48 bytes. None calls
a runtime helper during processing. Specialized helper symbols have external
linkage to preserve their four-register ABI and tail calls, but are not declared
in the public header. The general window helper assumes N>4, guaranteed by the public
dispatcher, eliminating the compiler's unreachable zero-tap clear call.
Live temporaries remain on the DTCM stack. Complete code ranges are checked from
each final image map.
The sixteen-output loop has four interleaved `vldrw`/scalar-coefficient vector
`vfma.f32` pairs per tap; the eight-output loop has two. Both use explicit DLS/LE
and no horizontal reduction. All asm-modified vector/GPR/condition registers
are clobbered; advancing pointers are early-clobber operands and memory is
clobbered. q4 is ABI-preserved by the emitted d8/d9 push/pop. No gather loads
are used. The tail retains explicit VPT predication.
Each full tile now seeds its accumulators with coefficient[0] multiplication,
executes N-2 middle taps in DLS/LE, then applies coefficient[N-1] and immediately
stores each completed output vector. All callers have N>8, so the middle count
is positive. The first/middle/final tap indices partition exactly [0,N); the
existing maximum offsets are unchanged. Early stores are safe because source,
coefficients, work and destination objects are disjoint. This reduces explicit
clearing and exposes final arithmetic/store overlap; performance still requires
whole-call measurement, including the larger medium-helper frame.
No change to source/coefficient arrays occurs during
processing.

Dynamic evidence must guard the complete N+127 window (including scratch),
alongside source, destination, original and prepared coefficients. The existing
32-byte MPU alignment and static-only instance/state limitations still apply.
The mandatory matrix includes 127/128/129 around the chunk boundary, all small
dimensions, odd taps and partial output vectors.

Small-block guard streams now contain at least256 samples plus eight blocks,
crossing multiple compactions while each individual buffer remains guarded.
The shared host/target variable-block lifecycle uses160 samples per independent
instance, crossing compaction with changing block sizes.

## Historical mirrored-ring audit

The audit below applies to the previous `fir-mirrored-mve-v1` candidate, not the
current linear-window implementation. It is retained as historical evidence.

This audit applies to `fir-mirrored-mve-v1`, the application-local candidate in
`src/kda_fir_f32.c`. It uses candidate-only AC6 6.24 Cortex-M55 Release disassembly
and the linked standard C runtime clear helper. No CMSIS-DSP instructions or
implementation details are inputs. Dynamic guard results must accompany this
ledger; this document alone is not safety qualification.

## Preconditions and object bounds

N is a positive uint16 value and B is positive. Every supplied array is a real,
sufficiently large, naturally aligned object with a representable byte span.
For f32 this requires B <= SIZE_MAX/4. All instance, state and array objects are
disjoint. The public and prepared coefficient arrays contain N floats, history
contains 2N, and source/destination contain B. The processing index p is initialized
to zero and remains in [0,N-1]. Values within the numerical contract are finite.

Byte intervals below are half-open. A maximum word start is three bytes before
the last byte of an access; the entire four-byte word must fit the interval.

| Operation | Reads | Writes | Maximum float index / byte interval |
|---|---|---|---|
| Count helpers | Argument registers | Return register | No buffer accesses; history count2N and prepared countN on 32-bit target |
| Rejected initialization | Argument registers and stack-passed arguments | Return register / function stack only | No caller object or array writes |
| Coefficient preparation | public C[N-1-k] | prepared P[k] | 0<=k<N; each array [base,base+4N) |
| Initialization/reset clear | No history reads | history H[0..2N) | Last index2N-1, bytes[H,H+8N) |
| Source sample | Src[i] | Registers | i<B, bytes[Src,Src+4B) |
| Mirrored insert | Registers | H[p],H[p+N] | Maximum index2N-1, last word begins H+8N-4 |
| Tap dot product | P[k+j],H[p+k+j] | Vector accumulator only | Active k+j<N; P maxN-1; H max2N-2, last word starts H+8N-8 |
| Destination sample | Accumulator | Dst[i] | i<B, bytes[Dst,Dst+4B) |
| History index publication | Registers | state.next | One size_t field inside state |

For the 32-bit target ABI, state contains `history` at offset0 and `next` at
offset4 (size8). Instance contains uint16 taps at offset0, prepared pointer at4
and state pointer at8 (size12). Initialization writes a halfword at instance+0,
two words at instance+4/+8, and two words at state+0/+4. Reset and processing
write only state+4. Padding at instance+2 is neither required nor modified.

## Emitted initialization and reset paths

The final initializer checks capacities, N and null pointers before buffer or
object stores. Loading its own stack arguments is allowed on failure. On success,
the compiler unrolls the reversal by four using scalar word loads/stores. Group t
reads C[N-1-4t-j] and writes P[4t+j] for j=0..3, only for complete groups.
Thus its lowest source index is nonnegative and highest destination index<N.
The three remaining scalar tail instructions execute only for N mod4 >=1,>=2,
and=3 respectively. Forming C+4N is one-past pointer arithmetic, not a read.

The initializer and reset both pass exactly 8N bytes and H to `__rt_memclr_w`.
The linked standard runtime entry sets the fill register to zero and falls
through to `_memset_w`. Its conditional pair of four-word STM stores handles
each complete32-byte chunk. Conditional16-byte and8-byte stores handle the
remaining byte count. Since 8N mod32 is one of {0,8,16,24}, its4/2/1-byte tails
are unreachable for these calls. Every store is inside [H,H+8N); no read is made
from H. The helper pushes/pops its own LR word and calls no other routine.

Candidate prologues/epilogues use ordinary allocated stack frames: init40bytes,
processing44bytes and reset8bytes, plus the clear helper's4bytes on a clear call.
These are not hidden caller buffer requirements. The target stack has mapped
headroom for the complete call and exception entry.

## Emitted processing paths

For valid N/B, the general path loads one scalar sample, writes both mirrors,
zeros q0 and executes `dlstp.32 lr,N`. The two `vldrw.u32` streams and `vfma.f32`
are inside the matching `letp` loop. The architecture tail predicate restricts
the last iteration to N mod4 active elements (four for an exact multiple).
Inactive lanes perform no buffer access and do not update the partial sum.
The same bound k+j<N applies to both loads. Pointer post-increments do not
themselves access memory after the last active word.

After the loop, scalar lane additions reduce q0; one scalar store writes Dst[i].
The index update chooses N when p==0 and otherwise p, then subtracts1, preserving
[0,N-1]. The outer loop increments i only while i<B. This path has no out-of-line
calls. Generated branches for N==0 or B==0 are outside the documented valid
domain and are not used to claim support for invalid calls.

The non-MVE host fallback has source-level scalar bounds matching the table.
Target qualification applies to the MVE build, not to an unbuilt scalar Arm
variant. No candidate size/alignment/padding contract was changed for testing.

## Guard protocol and its limits

The guard arena is DTCM and aligned32. Each selected buffer ends exactly at G;
[G,G+32) has no enabled MPU region. The preceding DTCM mapping ends inclusively
at G-1 and the following starts at G+32. Every other region is cleared before
installing explicit nonoverlapping code/data mappings. CTRL is ENABLE alone:
PRIVDEFENA=0 prevents privileged bypass and HFNMIENA=0 permits fault recording.
Caches are disabled with maintenance before changing attributes. Register
readback, original MPU/fault-control state and interrupt mask are retained;
the original mappings are restored before the oracle or logging.

For each of the323 matrix dimensions, each of five buffers (source, destination,
public coefficients, prepared coefficients, history) occupies the exact-end
position in turn. Four passes move the other mapped buffers through offsets
0/4/8/12 modulo16. Selected endpoints have the alignment determined by their
exact size and the32-byte MPU granularity; no padding is inserted before G.
In particular, an even-length history can only start at0/8 modulo16 when ending
at G. Its4/12 alignments are exercised while another buffer is guarded, and the
static address proof remains necessary for those placements.

Each case initializes while protected, processes eight blocks while protected,
checks independent double convolution after restoring mappings, and resets while
protected. Prefix and suffix sentinels are inspected after restoration, including
the then-readable gap. The suite contains6460 cases. State/instance field bounds
are established by the ABI/static audit; they are not falsely described as
dynamically guarded objects.

Separate images deliberately issue an unpredicated16-byte load from G-4 or a
volatile four-byte store at G. Expected evidence is a HardFault with forced
escalation, DACCVIOL and MMARVALID, MMFAR in the gap, and a stacked PC in that
negative-control operation. Missing faults, unrelated faults or healthy-run
canary/numerical failures leave qualification incomplete. Negative images halt
without recovering into the faulting instruction.

Public architectural/API references: CMSIS-Core6.3.0
`CMSIS/Core/Include/m-profile/armv8m_mpu.h` (RBAR/RLAR32-byte granularity,
MAIR, region programming, enable/disable side effects), `core_cm55.h` (MPU/SCB
bit definitions), and the compiler's public MVE intrinsic declarations. The
independent safety analysis is recorded in the active Humanize round summary.

## Recorded target results

AC6 6.24 Release on M55_HP passed all 6460 healthy cases, with zero failures and
live CFSR zero. The post-suite MPU CTRL and SHCSR matched their saved values.
The temporary configuration had 16 regions, CTRL=1 and PRIMASK=1; enabled regions
excluded exactly [0x20001500,0x20001520). The independent read and write images
both reached the recording handler with exception 3, CFSR=0x82 (DACCVIOL and
MMARVALID), HFSR=0x40000000 (FORCED), MMFAR=0x20001500 and unexpected=0.

The read-control stacked PC is 0x80202d94, the unpredicated vector load from
0x200014fc. The write-control PC is 0x80202af6, the scalar store at 0x20001500.
Both frames report EXC_RETURN=0xffffffe9. Debugger vector catch stopped at handler
entry first; continuing to the recording loop produced the saved fault records.
The controls establish dynamic read/write protection, alongside the static
proof and alignment limitations above. They do not claim that all alignments or
the instance/state objects were individually protected by an MPU boundary.

Evidence: `runs/fir-guard/healthy/`, `read-fault/`, and `write-fault/` contain the
separate AXF/HEX files, maps, hashes, build/load logs and raw/decoded results.
`healthy/candidate-disassembly.txt` covers only candidate functions and their
standard clear helper; the control listings contain only local harness code.
CMSIS Load logs confirm the intended HP and HE Release images and completed
programming; no byte-verification claim is made. Code executes from MRAM here,
so these are safety results only, with PMU/ITCM qualification still pending.

The Round 3 profiles under `runs/fir-profiles/` repeat the MRAM guard/control
runs with explicit archived mode headers and complete build inputs. The profiles
under `runs/fir-tcm/{guard,read-fault,write-fault}` additionally repeat them with
ITCM code and the scoped execute-only compiler option. All 6460 healthy cases
passed without live faults. Both controls again recorded CFSR 0x82 and forced
HardFault, now at the relocated gap 0x200015a0. The read PC is 0x1f78 and write PC
0x1d0e. Candidate and standard clear-helper disassembly was re-audited in
`fir-tcm/guard/candidate-disassembly.txt`: the same scalar reversal, exact clear,
predicated dot-product bounds and ABI field accesses apply after relocation.
These profile-specific manifests supersede the earlier mode-1-only source
manifest for reproducing the safety images; they do not retroactively change it.
