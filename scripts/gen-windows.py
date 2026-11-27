"""Lift a dumped Splatoon 3 ExeFS using the existing recompiler (no container).

Generated game code and images must stay in an external work directory. Run once
per dump; use a fresh work directory to regenerate without mixing old modules.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exefs', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--python-packages', type=Path,
                        help='optional isolated pip --target directory')
    parser.add_argument('--tu-insns', type=int, default=60000)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    root = repo / 'baseline' / 'totk-recomp'
    work = args.work.resolve()
    if work == repo or repo in work.parents:
        parser.error('--work must be outside the repository (generated game data is private)')
    if args.tu_insns <= 0:
        parser.error('--tu-insns must be positive')
    for module in ('main', 'sdk'):
        nso = args.exefs / module
        if not nso.is_file():
            parser.error(f'missing {nso}')
        with nso.open('rb') as f:
            header = f.read(0x60)
        if len(header) != 0x60 or header[:4] != b'NSO0':
            parser.error(f'{nso} is not an NSO')
        print(f'{module} build ID: {header[0x40:0x60].hex()}', flush=True)
    for name in ('img', 'funcs', 'gen'):
        if (work / name).exists():
            parser.error(f'{work / name} already exists; choose a fresh work directory')
    env = os.environ.copy()
    paths = [str(root / 'tools' / 'recomp')]
    if args.python_packages:
        paths.insert(0, str(args.python_packages.resolve()))
    if env.get('PYTHONPATH'):
        paths.append(env['PYTHONPATH'])
    env['PYTHONPATH'] = os.pathsep.join(paths)
    subprocess.run([sys.executable, '-c',
                    'import capstone; from importlib.metadata import version; '
                    'assert version("capstone") == "5.0.9", '
                    '"install capstone==5.0.9 for this recompiler"'], env=env, check=True)
    work.mkdir(parents=True, exist_ok=True)
    logs = work / 'logs'
    logs.mkdir(exist_ok=True)
    # Publish only complete main + SDK output. Interrupted/failed lifts cannot be
    # mistaken for a buildable source tree, and no existing output is deleted.
    with tempfile.TemporaryDirectory(prefix='generate-', dir=work) as staging:
        stage = Path(staging)
        for name in ('img', 'funcs', 'gen'):
            (stage / name).mkdir()
        for module in ('main', 'sdk'):
            image = stage / 'img' / module
            functions = stage / 'funcs' / f'funcs_{module}.json'
            output = stage / 'gen' / module
            extra = [] if module == 'main' else ['--base', '0x7180000000', '--prefix', 's', '--plt-all']
            commands = [
                [root / 'tools' / 'nso.py', args.exefs / module, image],
                [root / 'tools' / 'funcs.py', image, functions],
                [root / 'tools' / 'recomp' / 'gen.py', image, functions, output,
                 '--tu-insns', str(args.tu_insns), *extra],
            ]
            log = logs / f'gen_{module}.log'
            print(f'Lifting {module}; log: {log}', flush=True)
            with log.open('w', encoding='utf-8') as f:
                for command in commands:
                    subprocess.run([sys.executable, *map(str, command)], env=env,
                                   stdout=f, stderr=subprocess.STDOUT, check=True)
            report = json.loads((output / 'report.json').read_text())
            print(f"{module}: {report['functions']} functions, {report['insns']} instructions, "
                  f"{report['tus']} TUs, {report['unhandled_total']} unhandled instructions", flush=True)
        for name in ('img', 'funcs', 'gen'):
            (stage / name).rename(work / name)
    print(f'Generated main + SDK: {work / "gen"}', flush=True)


if __name__ == '__main__':
    main()
