# /// script
# requires-python = ">=3.13"
# dependencies = ["numpy==2.5.3"]
# ///
"""Report qualified TCM scaling, without changing whole-call parity acceptance.

Run with uv run --no-project scripts/report_fir_scaling.py --profile PROFILE
--capture CAPTURE --output OUTPUT_DIRECTORY. Each invocation revalidates the
capture and fits all 28 mandated asymptotic cells independently.
"""
import argparse
import csv
import json
import math
from pathlib import Path

import numpy as np

from report_fir import BLOCKS, TAPS, summarize

TARGET = 0.621


def fit(points):
    """Unweighted ordinary least squares on every supplied point; no filtering."""
    design = np.asarray([[p['block']*p['taps'], p['block'], p['taps'], 1]
                         for p in points], dtype=float)
    cycles = np.asarray([p['candidate_cycles'] for p in points], dtype=float)
    coefficients, _, rank, singular = np.linalg.lstsq(design, cycles, rcond=None)
    if rank != 4:
        raise ValueError('Rank-deficient asymptotic design')
    residual = cycles - design @ coefficients
    column_norms = np.linalg.norm(design, axis=0)
    return {
        'model': 'C=a*B*N+b*B+c*N+d', 'method': 'unweighted OLS, numpy.linalg.lstsq SVD',
        'point_count': len(points), 'rank': int(rank),
        'coefficients': dict(zip(('a', 'b', 'c', 'd'), coefficients.tolist())),
        'singular_values': singular.tolist(),
        'condition_number_2': float(singular[0]/singular[-1]),
        'column_normalized_condition_number_2': float(np.linalg.cond(design/column_norms)),
        'rank_relative_cutoff': float(max(design.shape)*np.finfo(float).eps),
        'residual_definition': 'measured minus fitted cycles',
        'residual_rms_cycles': float(np.sqrt(np.mean(residual**2))),
        'a_gap_from_target': float(coefficients[0]-TARGET),
        'a_percent_gap_from_target': float(100*(coefficients[0]/TARGET-1)),
        'residuals': [dict(block=p['block'], taps=p['taps'],
                           fitted_cycles=float(y-r), residual_cycles=float(r))
                      for p, y, r in zip(points, cycles, residual)],
    }


def metrics(row):
    b, n = row['block'], row['taps']
    c, baseline = (row[k]['median_cycles'] for k in ('candidate', 'baseline'))
    if not all(math.isfinite(v) and v > 0 for v in (c, baseline)):
        raise ValueError('Invalid median cycles')
    ratio = c/(b*n)
    return dict(block=b, taps=n, candidate_cycles=c, baseline_cycles=baseline,
                cycles_per_product=ratio, target_gap=ratio-TARGET,
                target_gap_percent=100*(ratio/TARGET-1), speedup=baseline/c,
                observed_parity=c <= baseline)


def trends(points, fixed, varying):
    result = []
    for value in sorted({p[fixed] for p in points}):
        group = sorted((p for p in points if p[fixed] == value), key=lambda p:p[varying])
        ratios = [p['cycles_per_product'] for p in group]
        deltas = [b-a for a,b in zip(ratios,ratios[1:])]
        direction = ('flat' if all(d == 0 for d in deltas) else
                     'nonincreasing' if all(d <= 0 for d in deltas) else
                     'nondecreasing' if all(d >= 0 for d in deltas) else 'mixed')
        result.append({fixed:value, 'varying_dimension':varying,
                       'values':[p[varying] for p in group],
                       'cycles_per_product':ratios, 'adjacent_changes':deltas,
                       'direction':direction, 'endpoint_change':ratios[-1]-ratios[0]})
    return result


def scaling(report):
    if not report['qualified'] or report['errors']:
        raise ValueError('Scaling target requires a qualified TCM capture')
    if report['metadata']['tcm_requested'] != 1:
        raise ValueError('0.621 target is not applicable outside verified TCM')
    rows = report['cases']
    expected = {(b,n) for b in BLOCKS for n in TAPS}
    if len(rows) != len(expected) or {(r['block'],r['taps']) for r in rows} != expected:
        raise ValueError('Incomplete or duplicate case matrix')
    if not all(r['stable'] and r['overhead_resolved'] for r in rows):
        raise ValueError('Unqualified matrix cell')
    all_points = [metrics(r) for r in rows]
    points = sorted((p for p in all_points if p['block'] >= 128 and p['taps'] >= 16),
                    key=lambda p:(p['block'],p['taps']))
    assert len(points) == 28
    return {
        'profile': report['profile'], 'build_id':report['metadata']['build_id'],
        'capture_qualified':True, 'target_cycles_per_product':TARGET,
        'metric':'raw-inclusive median cycles per whole processing call',
        'numpy_version':np.__version__,
        'acceptance_note':'One capture is not final acceptance. All 323 cases must meet parity in each of two qualified captures, with the separate pair checker passing.',
        'all_case_parity':all(p['observed_parity'] for p in all_points),
        'all_case_parity_failures':sum(not p['observed_parity'] for p in all_points),
        'points':points, 'fit':fit(points),
        'rows_increasing_taps':trends(points,'block','taps'),
        'columns_increasing_block':trends(points,'taps','block'),
        'onset_neighbors':sorted((p for p in all_points
                                  if p['block'] in (127,128,129) and p['taps'] in (15,16,17)),
                                 key=lambda p:(p['block'],p['taps'])),
    }


def write_outputs(result, output):
    output.mkdir(parents=True, exist_ok=True)
    (output/'scaling.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    residuals = {(r['block'],r['taps']):r for r in result['fit']['residuals']}
    rows = [{**p, **residuals[p['block'],p['taps']]} for p in result['points']]
    with (output/'scaling.csv').open('w',newline='') as stream:
        writer = csv.DictWriter(stream,fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    lines = ['# FIR TCM scaling', '', result['acceptance_note'], '',
             f"Capture qualified: yes. All-case parity failures: {result['all_case_parity_failures']}/323.",
             f"Metric: {result['metric']}. Code in ITCM; computation data in DTCM.",
             f"Build identity: `{result['build_id']}`.", '',
             '## Complete asymptotic region', '',
             '| B | N | Candidate cycles | CMSIS cycles | C/(BN) | Gap to 0.621 | Gap % | Speedup | Fit residual cycles |',
             '|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    for r in rows:
        lines.append(f"| {r['block']} | {r['taps']} | {r['candidate_cycles']:.3f} | {r['baseline_cycles']:.3f} | {r['cycles_per_product']:.6f} | {r['target_gap']:+.6f} | {r['target_gap_percent']:+.3f} | {r['speedup']:.4f} | {r['residual_cycles']:+.3f} |")
    model = result['fit']
    lines += ['', '## Auxiliary fit', '',
              'Unweighted OLS uses all 28 points; full-call measurements above remain primary.',
              'The fitted coefficient a is an estimate, not a measured full-call limit or parity waiver.', '',
              '`C=a*B*N+b*B+c*N+d`', '',
              ', '.join(f'{k}={v:.9g}' for k,v in model['coefficients'].items())+'.',
              f"Rank {model['rank']}/4; raw design condition number (2-norm) {model['condition_number_2']:.6g}; column-normalized condition number {model['column_normalized_condition_number_2']:.6g}.",
              f"Residual RMS: {model['residual_rms_cycles']:.6f} cycles. Residuals are measured minus fitted.",
              f"Fitted a gap: {model['a_gap_from_target']:+.6f} cycles/product ({model['a_percent_gap_from_target']:+.3f}%).", '',
              '## Row and column trends', '']
    for name, fixed in [('rows_increasing_taps','block'),('columns_increasing_block','taps')]:
        for group in result[name]:
            series = ', '.join(f'{x}:{y:.6f}' for x,y in zip(group['values'],group['cycles_per_product']))
            lines += [f"- {fixed}={group[fixed]}, increasing {group['varying_dimension']}: {series}; {group['direction']}; endpoint change {group['endpoint_change']:+.6f}."]
    lines += ['', '## Onset and neighbors', '',
              '| B | N | Candidate cycles | CMSIS cycles | C/(BN) | Gap | Gap % | Speedup |',
              '|---:|---:|---:|---:|---:|---:|---:|---:|']
    for r in result['onset_neighbors']:
        lines.append(f"| {r['block']} | {r['taps']} | {r['candidate_cycles']:.3f} | {r['baseline_cycles']:.3f} | {r['cycles_per_product']:.6f} | {r['target_gap']:+.6f} | {r['target_gap_percent']:+.3f} | {r['speedup']:.4f} |")
    (output/'scaling.md').write_text('\n'.join(lines)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',required=True,type=Path)
    parser.add_argument('--capture',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args = parser.parse_args()
    result = scaling(summarize(args.profile,args.capture))
    result['capture'] = str(args.capture)
    write_outputs(result,args.output)
    print(json.dumps({'output':str(args.output), 'points':len(result['points']),
                      'all_case_parity':result['all_case_parity'], 'fit':result['fit']['coefficients']},indent=2))
