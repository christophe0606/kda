"""Decode supported MCP memory export and report inclusive PMU diagnostics.

Input: capture/memory.json with address and bytes; build evidence in --profile.
No board access. Results never subtract overhead to claim parity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import struct

BLOCKS = [1,2,3,4,5,7,8,15,16,17,31,32,63,64,127,128,129,256,512]
TAPS = [1,2,3,4,5,6,7,8,9,15,16,17,31,32,33,64,128]


def summarize(profile, capture):
    manifest = json.loads((profile / 'manifest.json').read_text())
    raw = json.loads((capture / 'memory.json').read_text())
    data = bytes(raw['bytes'])
    if len(data) != 125588:
        raise ValueError('Incomplete or unsupported memory export')
    words = struct.unpack('<' + 'I' * (len(data)//4), data)
    names = ('magic version bytes phase cases_completed failures mode tcm_requested '
             'system_clock fpscr control primask msp psp ccr pmu_type pmu_auth pmu_ctrl '
             'pmu_filter pmu_enable pmu_irq probe_short probe_long probe_frozen probe_wrap endpoint_cycles').split()
    meta = dict(zip(names, words[:26]))
    meta['code'] = list(words[26:31])
    meta['data'] = [list(words[i:i+2]) for i in range(31,55,2)]
    meta.update(zip(('seed warmup_calls maximum_interval saved_ctrl saved_filter saved_enable '
                     'saved_irq saved_overflow saved_ccntr restored faults').split(), words[55:66]))
    errors = []
    if words[0] != 0x504d5531 or words[1] != 1 or words[2] != len(data):
        errors.append('wire format/magic mismatch')
    if not (meta['phase'] == 3 and meta['cases_completed'] == 323 and meta['failures'] == 0
            and meta['restored'] == 1 and meta['faults'] == 0):
        errors.append('incomplete or failed target run')
    if not manifest['tcm'] or manifest['mode'] != 'benchmark' or meta['tcm_requested'] != 1:
        errors.append('non-TCM profile')
    if not all(0 < p < 0x40000 for p in meta['code']):
        errors.append('entry point outside ITCM')
    if not all(0x20000000 <= p < p+n <= 0x20100000 for p,n in meta['data']):
        errors.append('data outside DTCM')
    if not 0x20000000 < meta['msp'] <= 0x20100000:
        errors.append('stack outside DTCM')
    if not (meta['pmu_type'] & 0x4000 and meta['pmu_ctrl'] == 1 and meta['pmu_irq'] == 0
            and meta['probe_frozen'] == 0 and meta['probe_wrap'] & 0x80000000
            and meta['probe_long'] > meta['probe_short'] >= 1024):
        errors.append('PMU qualification failed')
    for name, digest in manifest['artifacts_sha256'].items():
        p = profile / name
        if not p.exists() or hashlib.sha256(p.read_bytes()).hexdigest() != digest:
            errors.append('artifact mismatch: ' + name)
    for name in ['FilteringFunctions.o', 'kda_fir_f32.o', 'fir_batch_candidate.o', 'fir_batch_baseline.o']:
        section_file = profile / (name + '.sections.txt')
        if not section_file.exists() or 'AXy' not in section_file.read_text():
            errors.append('pure-code provenance absent: ' + name)
    for name, digest in manifest['inputs_sha256'].items():
        p = profile / 'inputs' / name
        if not p.exists() or hashlib.sha256(p.read_bytes()).hexdigest() != digest:
            errors.append('input mismatch: ' + name)
    commands = json.loads((profile / 'out/kda/DevKit-E8/Release/compile_commands.json').read_text())
    for source in ['kda_fir_f32.c', 'FilteringFunctions.c', 'fir_batch_candidate.c', 'fir_batch_baseline.c']:
        matches = [c['command'] for c in commands if c['file'].replace('\\','/').endswith('/'+source)]
        if len(matches) != 1 or not all(flag in matches[0] for flag in
                                        ['-O3', '-ffast-math', '-fno-lto', '-mexecute-only']):
            errors.append('compiler flags missing: ' + source)
        elif 'ARM_MATH_AUTOVECTORIZE' in matches[0]:
            errors.append('unexpected comparator build selection')
    load = (capture / 'load.log').read_text(encoding='utf-8-sig', errors='replace')
    if not all(text in load for text in ('\\kda\\DevKit-E8\\Release\\kda.hex',
                                         '\\M55_HE\\DevKit-E8\\Release\\M55_HE.hex', 'programmed')):
        errors.append('load completion evidence missing')
    rows = []
    for i, (block, taps) in enumerate((b,n) for b in BLOCKS for n in TAPS):
        values = words[66+i*97:66+(i+1)*97]
        b,n,reps,flags = values[:4]
        if (b,n) != (block,taps) or not reps or flags:
            raise ValueError(f'Missing/invalid matrix case {block},{taps}')
        row = {'block':b, 'taps':n, 'repetitions':reps, 'flags':flags}
        for j, name in enumerate(['empty', 'candidate', 'baseline']):
            raw_batches = list(values[4+j*31:4+(j+1)*31])
            if not all(0 < x < meta['maximum_interval'] for x in raw_batches):
                raise ValueError(f'invalid interval: {b},{n},{name}')
            median = statistics.median(raw_batches)
            mad = statistics.median(abs(x-median) for x in raw_batches)
            row[name] = {'raw':raw_batches, 'median_cycles':median/reps,
                         'min_cycles':min(raw_batches)/reps, 'max_cycles':max(raw_batches)/reps,
                         'mad_fraction':mad/median if median else None}
        a, c, empty = (row[k]['median_cycles'] for k in ['candidate', 'baseline', 'empty'])
        row['endpoint_fraction'] = meta['endpoint_cycles']/reps/min(a,c)
        row['empty_fraction'] = empty/min(a,c)
        row['stable'] = all(row[k]['mad_fraction'] <= .01 for k in ['candidate','baseline'])
        row['overhead_resolved'] = row['empty_fraction'] <= .01 and row['endpoint_fraction'] <= .01
        row['observed_parity'] = a <= c
        row['candidate_corrected_diagnostic'] = a-empty
        row['baseline_corrected_diagnostic'] = c-empty
        rows.append(row)
    result = {'metadata':meta, 'profile':str(profile), 'errors':errors,
              'qualified':not errors and all(r['stable'] and r['overhead_resolved'] for r in rows),
              'unstable_cases':sum(not r['stable'] for r in rows),
              'overhead_unresolved_cases':sum(not r['overhead_resolved'] for r in rows),
              'observed_parity_failures':sum(not r['observed_parity'] for r in rows), 'cases':rows}
    (capture / 'report.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ['metadata','cases']}, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--capture', type=Path, required=True)
    args = parser.parse_args()
    summarize(args.profile, args.capture)
