# Candidate buffer-access ledger

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
