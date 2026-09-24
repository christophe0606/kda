"""Plant invalid evidence into a copy of a real capture and require rejection."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import shutil
import struct
from report_fir import summarize


def check(profile, capture, output):
    output.mkdir(parents=True, exist_ok=True)
    original = json.loads((capture / 'memory.json').read_text())
    words = list(struct.unpack('<'+'I'*(len(original['bytes'])//4), bytes(original['bytes'])))
    changes = {
        'false_tcm': {7:0}, 'incomplete': {4:322}, 'frozen_counter': {22:0},
        'fault': {65:1}, 'missing_case': {66:2}, 'overflow': {70+31:0x10000000},
        'wrong_code_region': {26:0x80201001}, 'wrong_data_region': {31:0x02000000},
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
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                result = summarize(profile, target)
            rejected = bool(result['errors'])
        except ValueError:
            rejected = True
        if not rejected:
            raise AssertionError('Failed to reject ' + name)
    print(f'{len(changes)}/{len(changes)} invalid-evidence controls rejected')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', required=True, type=Path)
    parser.add_argument('--capture', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    check(args.profile, args.capture, args.output)
