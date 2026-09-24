"""Prepare supported MCP read requests, without accessing the board."""
import argparse
import json
from pathlib import Path
from fir_evidence import expected_readback, map_metadata, sha

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    requests = [{'name':name, 'address':hex(address), 'length':len(data), 'sha256':sha(data)}
                for name,address,data in expected_readback(args.profile)]
    symbols, _, _, _ = map_metadata(args.profile / 'out/kda/DevKit-E8/Release/kda.axf.map')
    args.output.mkdir(parents=True, exist_ok=True)
    record = {'readback':requests, 'result':symbols['kda_fir_benchmark'],
              'numerical':symbols['kda_fir_result']}
    (args.output/'requests.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record))
