# FIR target results

Measured source: `97194ca32110e32955f37bdc070d1b49a0741f2f`
(`fir-tiny4-gpr-window-v1`). AC6 6.24,Release,-O3/-ffast-math,no LTO,
Alif E8 M55_HP,400 MHz. FIR code/call path in ITCM;computation data in DTCM.

Both independent whole-call PMU captures pass the
[user-approved absolute two-cycle limit](fir-acceptance.md) in all323 cells.
Each has31 paired batches,zero protocol errors,unstable cases or unresolved
overhead,and47 image/timed-code readback blocks. Maximum cross-capture drift is
0.000035101%,below1%. No timing or result subtraction was used.

Strict comparisons are retained:322/323 cells have candidate<=CMSIS in each run.
The exception is B5/N4:72.05078125 versus71.0517578125 cycles,a0.9990234375-cycle
difference. B7/N4 passes at70.05078125 versus71.0517578125. Earlier strict
rejections remain historical records;this acceptance uses the explicit amendment.

Host7/7,target2261 numerical cases plus lifecycle,6460 healthy MPU cases,both
deliberate read/write fault controls,and complete owned bounds/disassembly audit
pass. Exact safety readback counts38/34/12/11. Processing instructions agree
across numerical,guard and benchmark profiles. CMSIS remains an opaque comparator.
The installed independent T9 review passed under the explicit user amendment,
with no acceptance blocker. Its retained output is
`.humanize/skill/2026-09-25_15-05-32-1261-cd70e26d/output.md`.

## Asymptotic report

All28 B>=128,N>=16 points are included separately for each capture,including
signed target gaps,speedups,row/column trends and onset neighbors. Example values:

| B | N | Candidate cycles | CMSIS cycles | Cycles/(B*N) | Gap to0.621 | Gap percent |
|---:|---:|---:|---:|---:|---:|---:|
|128|16|1514.226563|1562.242188|0.739368|+0.118368|+19.06094%|
|512|128|43196.234375|44081.25|0.659122|+0.038122|+6.13885%|

Both independent fits over all28 points yield
`C=0.646525053337*B*N+0.747510165300*B+1.13383111123*N+141.507903765`.
This auxiliary estimate does not replace measured full-call results. Complete
residuals,RMS,rank/conditioning,trends and onset neighborhood are retained below.
The remaining target gap is reported;0.621 is an asymptotic target,not a hard
acceptance tolerance. External-memory/cache profiles are not mixed into this fit.

## Reproduce and locate evidence

- Build/profile and safety: `runs/fir-r9/gpr-window-{benchmark,numerical,guard,read-fault,write-fault}`.
- Capture1: `runs/fir-r9/gpr-window-capture-1`.
- Independent capture2: `runs/fir-r10/final-capture-2`.
- Both captures contain raw memory,numerical record,opaque image readback,load/device
  evidence,report.json and scaling/{scaling.json,scaling.csv,scaling.md}.
- Pair: `runs/fir-r10/final-pair.json`,with explicit allowance,strict results,
  both per-case signed differences and drift fractions.
- All646 measured case rows with raw medians and signed differences:
  `runs/fir-r10/final-cycles.csv`.
- Checker controls: `runs/fir-r10/allowance-controls`:18 original rejection
  controls,a strict synthetic positive,and10 allowance/boundary/protocol controls.

```powershell
uv run --no-project --python 3.13 python scripts/compare_fir.py --profile runs/fir-r9/gpr-window-benchmark --captures runs/fir-r9/gpr-window-capture-1 runs/fir-r10/final-capture-2 --parity-allowance-cycles 2 --output runs/fir-r10/final-pair.json
```

The checker defaults to strict zero allowance. Scaling files keep their original
strict all-case-parity fields;interpret final acceptance using the explicit pair
policy. Firmware inputs are unchanged from97194ca;later commits update evidence
and offline checking only. Generated evidence is local and ignored by Git.
