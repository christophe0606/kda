"""Plant invalid evidence into a copy of a real capture and require rejection."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import shutil
import struct
import copy
from compare_fir import compare
from report_fir import summarize


def check(profile, capture, output):
    output.mkdir(parents=True, exist_ok=True)
    original = json.loads((capture / 'memory.json').read_text())
    words = list(struct.unpack('<'+'I'*(len(original['bytes'])//4), bytes(original['bytes'])))
    changes = {
        'false_tcm': {7:0}, 'incomplete': {4:322}, 'frozen_counter': {22:0},
        'fault': {65:1}, 'missing_case': {66:2}, 'overflow': {70+31:0x10000000},
        'wrong_code_region': {26:0x80201001}, 'wrong_data_region': {31:0x02000000},
        'wrong_image_identity': {31397:0},
    }
    for name, mutations in changes.items():
        target = output / name
        target.mkdir(exist_ok=True)
        altered = words.copy()
        for index, value in mutations.items():
            altered[index] = value
        record = dict(original, bytes=list(struct.pack('<'+'I'*len(altered), *altered)))
        (target / 'memory.json').write_text(json.dumps(record))
        shutil.copyfile(capture / 'load.log', target / 'load.log')
        shutil.copyfile(capture / 'image-readback.json', target / 'image-readback.json')
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                result = summarize(profile, target)
            rejected = bool(result['errors'])
        except ValueError:
            rejected = True
        if not rejected:
            raise AssertionError('Failed to reject ' + name)
    for name in ('wrong_export_address','wrong_programmed_image'):
        target = output/name
        target.mkdir(exist_ok=True)
        record = copy.deepcopy(original)
        images = json.loads((capture/'image-readback.json').read_text())
        if name == 'wrong_export_address':
            record['address'] = hex(int(record['address'],0)+4)
        else:
            images['blocks'][0]['bytes'][0] ^= 1
        (target/'memory.json').write_text(json.dumps(record))
        (target/'image-readback.json').write_text(json.dumps(images))
        shutil.copyfile(capture/'load.log',target/'load.log')
        with contextlib.redirect_stdout(io.StringIO()):
            assert summarize(profile,target)['errors'], name
    with contextlib.redirect_stdout(io.StringIO()):
        good = summarize(profile,capture)
    assert good['qualified'], {k:v for k,v in good.items() if k not in ('cases','metadata')}
    real_pair = compare(good,good)
    assert real_pair['qualified_pair'] == all(r['observed_parity'] for r in good['cases'])
    # An explicitly synthetic in-memory control exercises successful acceptance
    # even when the real capture still has losses. Never export it as evidence.
    passing = copy.deepcopy(good)
    for row in passing['cases']:
        row['candidate']['median_cycles'] = min(row['candidate']['median_cycles'],
                                                row['baseline']['median_cycles'])
        row['observed_parity'] = True
    assert compare(passing,passing)['qualified_pair']
    for name in ('missing_case','wrong_identity','changed_reps','drift','parity'):
        changed = copy.deepcopy(passing)
        if name == 'missing_case': changed['cases'].pop()
        elif name == 'wrong_identity': changed['metadata']['build_id'] = 'wrong'
        elif name == 'changed_reps': changed['cases'][0]['repetitions'] *= 2
        elif name == 'drift': changed['cases'][0]['candidate']['median_cycles'] *= 1.02
        else:
            changed['cases'][0]['candidate']['median_cycles'] = changed['cases'][0]['baseline']['median_cycles'] * 1.001
            changed['cases'][0]['observed_parity'] = False
        result = compare(passing,changed)
        if name == 'parity':
            # Identical complete captures have zero drift: parity alone must
            # prevent qualification, including when only the first run fails.
            for first,second in ((changed,changed),(changed,passing),(passing,changed)):
                pair = compare(first,second)
                assert not pair['parity_both'] and not pair['qualified_pair']
            assert not compare(changed,changed)['errors']
        else: assert result['errors'], name
    print(f'{len(changes)+9} invalid-evidence/pair controls rejected; synthetic positive pair control passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', required=True, type=Path)
    parser.add_argument('--capture', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    check(args.profile, args.capture, args.output)
