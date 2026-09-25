# FIR performance acceptance amendment

On2026-09-25 the user accepted a difference of one or two cycles versus
CMSIS-DSP. The fixed limit used for final qualification is therefore:

`candidate_median_cycles <= CMSIS_median_cycles + 2.0`

This absolute allowance applies uniformly to every mandatory matrix cell in
each independent capture of the whole public processing call. It is not a
percentage, aggregate allowance, or subtraction from measured cycles.

Strict candidate<=CMSIS comparisons,raw measurements and signed differences
remain reported. Earlier strict-parity rejection records are retained as history.
Correctness,safety,placement,image identity,PMU protocol,dispersion and<=1%
cross-capture drift requirements are unchanged. The0.621 target and its complete
28-point scaling analysis remain unchanged and apply only to verified ITCM/DTCM.

This user amendment supersedes the strict performance threshold in the tracked
historical docs/plan.md and Round0 review. The original plan remains unchanged
under the active RLCR plan-file integrity rule;the mutable tracker and round
contract reference this explicit amendment.
