"""Select, validate, build and archive a FIR Release profile. No board access.

Run with uv run --no-project --python 3.13 scripts/build_fir.py MODE --output runs/...
The caller must serialize this with CMSIS build/load/debug operations.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MODES = {'demo': 0, 'numerical': 1, 'guard': 2, 'read-fault': 3,
         'write-fault': 4, 'benchmark': 5}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(args):
    os.chdir(ROOT)
    destination = (ROOT / args.output).resolve()
    if not any(destination.is_relative_to(ROOT / p) for p in ('runs', 'outputs', 'profile')):
        raise ValueError('Evidence output must be inside runs/, outputs/ or profile/')
    destination.mkdir(parents=True, exist_ok=True)
    if (destination / 'manifest.json').exists():
        raise ValueError('Do not overwrite an existing capture; choose a fresh output directory')
    env = os.environ.copy()
    env.pop('VIRTUAL_ENV', None)
    config = (ROOT / '.cmsis/tools-environment.yml').read_text()
    paths = re.findall(r'^      - ([^\r\n]+)$', config, re.M)
    env['PATH'] = os.pathsep.join(paths + [env['PATH']])
    for name in ('CMSIS_PACK_ROOT', 'AC6_TOOLCHAIN_6_24_0'):
        env[name] = re.search(r'^      ' + name + r': (.+)$', config, re.M)[1].strip()
    toolbox = next(Path(p) for p in paths if 'cmsis-toolbox/bin' in p)
    header = ('#ifndef KDA_FIR_PROFILE_H\n#define KDA_FIR_PROFILE_H\n'
              '/* Selected by scripts/build_fir.py; archived with every image. */\n'
              f'#define KDA_APP_FIR {MODES[args.mode]}\n'
              f'#define KDA_FIR_TCM {int(args.tcm)}\n#endif\n')
    (ROOT / 'src/fir_profile.h').write_text(header, encoding='utf-8')
    inputs = [p for directory in ('src', 'tests', 'board', 'M55_HE', 'scripts')
              for p in (ROOT / directory).rglob('*')
              if p.is_file() and p.suffix in ('.c', '.h', '.sct', '.yml', '.py')]
    inputs += [ROOT / p for p in ('kda.cproject.yml', 'kda.csolution.yml',
                                 '.cmsis/tools-environment.yml')]
    before = {p: digest(p) for p in set(inputs)}
    commands = [
        ('validation.log', [str(toolbox / 'csolution.exe'), 'convert',
                            'kda.csolution.yml', '--active', 'DevKit-E8@Release', '--no-update-rte']),
        ('build.log', [str(toolbox / 'cbuild.exe'), 'kda.csolution.yml', '--target', 'all',
                       '--active', 'DevKit-E8@Release', '--packs'])]
    for log, command in commands:
        with (destination / log).open('w', encoding='utf-8') as output:
            status = subprocess.run(command, env=env, stdout=output, stderr=subprocess.STDOUT).returncode
        print(f'{log}: exit {status}', flush=True)
        if status:
            print((destination / log).read_text()[-10000:])
            return status
    # Copy local inputs only. DSP sources/headers are opaque dependencies, never read.
    if any(digest(p) != h for p, h in before.items()):
        raise RuntimeError('An input changed during validation/build; reject the archive and rebuild')
    source_hashes = {}
    for p in sorted(set(inputs)):
        relative = p.relative_to(ROOT)
        copied = destination / 'inputs' / relative
        copied.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, copied)
        source_hashes[relative.as_posix()] = digest(p)
    artifacts = ['kda.cbuild-idx.yml', 'out/kda+DevKit-E8.cbuild-run.yml']
    for project in ('kda', 'M55_HE'):
        base = f'out/{project}/DevKit-E8/Release'
        artifacts += [f'{base}/{project}.{suffix}' for suffix in ('axf', 'hex', 'axf.map')]
        artifacts += [f'{base}/compile_commands.json', f'{base}/{project}.Release+DevKit-E8.cbuild.yml']
    artifact_hashes = {}
    for name in artifacts:
        p = ROOT / name
        copied = destination / name
        copied.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, copied)
        artifact_hashes[name] = digest(p)
    scatter = ROOT / 'board/DevKit-E8/M55_HP/RTE/Device/AE822FA0E5597LS0_M55_HP/linker_ac6_mram.sct'
    compiler = Path(env['AC6_TOOLCHAIN_6_24_0']) / 'armclang.exe'
    with (destination / 'scatter-preprocessed.txt').open('w') as output:
        # The first line is an armlink command directive, not C input.
        text = '\n'.join(scatter.read_text().splitlines()[1:])
        subprocess.run([str(compiler), '-E', '--target=arm-arm-none-eabi',
                        '-mcpu=cortex-m55', '-xc', '-I', str(scatter.parent), '-'],
                       input=text, text=True, env=env, stdout=output, check=True)
    artifact_hashes['scatter-preprocessed.txt'] = digest(destination / 'scatter-preprocessed.txt')
    llvm = next(Path(p) for p in paths if 'compilers.arm.llvm.embedded' in p)
    for object_name, symbols in (
            ('FilteringFunctions.o', ['.text.arm_fir_f32']),
            ('kda_fir_f32.o', ['.text.kda_fir_f32']),
            ('fir_batch_candidate.o', ['.text.fir_batch_candidate']),
            ('fir_batch_baseline.o', ['.text.fir_batch_baseline'])):
        obj = next((ROOT / 'build/cmsis/tmp/1').rglob(object_name))
        # ELF section flags/symbol identity only; never disassemble the comparator.
        sections = subprocess.check_output([str(llvm / 'llvm-readelf.exe'), '--sections', str(obj)], text=True)
        selected = '\n'.join(line for line in sections.splitlines()
                             if any(symbol in line for symbol in symbols)) + '\n'
        (destination / (object_name + '.sections.txt')).write_text(selected)
        object_copy = destination / obj.relative_to(ROOT)
        object_copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(obj, object_copy)
        artifact_hashes[obj.relative_to(ROOT).as_posix()] = digest(obj)
    manifest = {'mode': args.mode, 'mode_value': MODES[args.mode], 'tcm': args.tcm,
                'target': 'DevKit-E8@Release', 'commands': commands,
                'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                'inputs_sha256': source_hashes, 'artifacts_sha256': artifact_hashes,
                'source_manifest_sha256': hashlib.sha256(json.dumps(source_hashes, sort_keys=True).encode()).hexdigest()}
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    # Conversion can regenerate tasks. Restore log capture for the supported MCP launch.
    task_path = ROOT / '.vscode/tasks.json'
    tasks = json.loads(task_path.read_text(encoding='utf-8-sig'))
    if not any(t['label'] == 'CMSIS Prepare logs' for t in tasks['tasks']):
        tasks['tasks'].insert(0, {'label': 'CMSIS Prepare logs', 'type': 'process',
            'command': 'cmd.exe', 'args': ['/d', '/c', 'if not exist runs mkdir runs'],
            'options': {'cwd': '${workspaceFolder}/'}, 'problemMatcher': []})
    load = next(t for t in tasks['tasks'] if t['label'] == 'CMSIS Load')
    load['command'] = ('pyocd load --probe cmsisdap: --cbuild-run '
                       '"${command:cmsis-csolution.getCbuildRunFile}" '
                       '--log "*.cbuild_run=info" > runs/cmsis-load.log 2>&1')
    load['dependsOn'] = 'CMSIS Prepare logs'
    load.pop('args', None)
    task_path.write_text(json.dumps(tasks, indent=4) + '\n', encoding='utf-8')
    print(f'Archived {args.mode}, TCM={args.tcm}: {destination}', flush=True)
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=MODES)
    parser.add_argument('--tcm', action='store_true')
    parser.add_argument('--output', required=True)
    sys.exit(build(parser.parse_args()))
