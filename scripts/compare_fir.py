"""Reproducibly compare two complete captures of the same committed image."""
import argparse
import json
from pathlib import Path
from report_fir import summarize


def compare(a, b):
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
        for kernel in ('candidate','baseline'):
            c,d = x[kernel]['median_cycles'], y[kernel]['median_cycles']
            drift = abs(c-d)/min(c,d)
            row[kernel+'_drift_fraction'] = drift
            if drift > .01:
                errors.append(f'cross-capture drift: {x["block"]},{x["taps"]},{kernel}')
        rows.append(row)
    parity_both = all(r['observed_parity'] for r in a['cases']+b['cases'])
    return {'errors':errors,'qualified_pair':not errors and parity_both,
            'parity_both':not errors and parity_both,
            'cases':rows}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',required=True,type=Path)
    parser.add_argument('--captures',required=True,type=Path,nargs=2)
    parser.add_argument('--output',required=True,type=Path)
    args = parser.parse_args()
    result = compare(*(summarize(args.profile,c) for c in args.captures))
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='cases'},indent=2))
