"""Reproducibly compare two complete captures of the same committed image."""
import argparse
import json
import math
from pathlib import Path
from report_fir import summarize


def compare(a, b, parity_allowance_cycles=0.0):
    """Apply an explicit absolute whole-call allowance; retain strict results."""
    if not math.isfinite(parity_allowance_cycles) or not 0 <= parity_allowance_cycles <= 2:
        raise ValueError('parity allowance must be finite and between 0 and 2 cycles')
    errors = []
    if not a['qualified'] or not b['qualified']:
        errors.append('unqualified input capture')
    for field in ('build_id','system_clock','fpscr','control','primask','ccr',
                  'pmu_type','pmu_auth','pmu_ctrl','pmu_filter','pmu_irq',
                  'seed','warmup_calls','maximum_interval','code','data'):
        if a['metadata'][field] != b['metadata'][field]:
            errors.append('capture protocol mismatch: '+field)
    rows = []
    if len(a['cases']) != 323 or len(b['cases']) != 323:
        errors.append('incomplete matrix')
    for x,y in zip(a['cases'],b['cases']):
        if any(x[k] != y[k] for k in ('block','taps','repetitions')):
            errors.append('case/repetition mismatch')
        row = {'block':x['block'],'taps':x['taps']}
        for label, case in (('first', x), ('second', y)):
            delta = case['candidate']['median_cycles'] - case['baseline']['median_cycles']
            row[label+'_delta_cycles'] = delta
            row[label+'_strict_parity'] = delta <= 0
            row[label+'_accepted_parity'] = delta <= parity_allowance_cycles
        for kernel in ('candidate','baseline'):
            c,d = x[kernel]['median_cycles'], y[kernel]['median_cycles']
            drift = abs(c-d)/min(c,d)
            row[kernel+'_drift_fraction'] = drift
            if drift > .01:
                errors.append(f'cross-capture drift: {x["block"]},{x["taps"]},{kernel}')
        rows.append(row)
    parity_both = all(r[label+'_accepted_parity'] for r in rows
                      for label in ('first', 'second'))
    strict_both = all(r[label+'_strict_parity'] for r in rows
                      for label in ('first', 'second'))
    return {'errors':errors,'qualified_pair':not errors and parity_both,
            'parity_both':not errors and parity_both,
            'strict_parity_both':not errors and strict_both,
            'parity_allowance_cycles':parity_allowance_cycles,
            'parity_definition':'candidate median <= baseline median + absolute allowance',
            'accepted_failures':{label:sum(not r[label+'_accepted_parity'] for r in rows)
                                 for label in ('first', 'second')},
            'strict_failures':{label:sum(not r[label+'_strict_parity'] for r in rows)
                               for label in ('first', 'second')},
            'cases':rows}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',required=True,type=Path)
    parser.add_argument('--captures',required=True,type=Path,nargs=2)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--parity-allowance-cycles',type=float,default=0.0,
                        help='Explicit absolute whole-call allowance, 0 by default; user-approved maximum 2')
    args = parser.parse_args()
    result = compare(*(summarize(args.profile,c) for c in args.captures),
                     parity_allowance_cycles=args.parity_allowance_cycles)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='cases'},indent=2))
