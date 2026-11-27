"""Run the native Windows build with isolated saves and a bounded smoke-test log."""
import argparse
import datetime
from pathlib import Path
import os
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--romfs', type=Path, required=True,
                        help='merged Splatoon 3 RomFS matching the lifted ExeFS')
    parser.add_argument('--headless', action='store_true')
    parser.add_argument('--seconds', type=int, default=120,
                        help='stop after this many seconds; 0 runs until the window closes')
    args = parser.parse_args()
    if args.seconds < 0:
        parser.error('--seconds must be nonnegative')
    work = args.work.resolve()
    repo = Path(__file__).resolve().parents[1]
    if work == repo or repo in work.parents:
        parser.error('--work must be outside the repository')
    romfs = args.romfs.resolve()
    if not (romfs / 'Pack' / 'Params.pack.zs').is_file():
        parser.error('RomFS is missing Pack/Params.pack.zs; supply a merged Splatoon 3 dump, not Splatoon 2 RomFS')
    backend = 'build-null' if args.headless else 'build-vk'
    exe = work / backend / 'inksplash3.exe'
    for path in (exe, work / 'img' / 'main.bin', work / 'img' / 'sdk.bin'):
        if not path.is_file():
            parser.error(f'missing {path}; generate and build first')
    logs = work / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    userdata = work / 'userdata'
    userdata.mkdir(exist_ok=True)
    log = logs / f'{backend}-{datetime.datetime.now():%Y%m%d-%H%M%S-%f}.log'
    env = os.environ.copy()
    env.update(TOTK_IMG=str(work / 'img'), TOTK_ROMFS=str(romfs), TOTK_USERDATA=str(userdata),
               TOTK_LIFT_ALL='1', TOTK_NO_OVERLAY='1', TOTK_SKIP_OPENING='0', TOTK_SETTINGS='0')
    if args.headless:
        env['TOTK_HEADLESS'] = '1'
    else:
        env.pop('TOTK_HEADLESS', None)
        env.setdefault('TOTK_PC_INPUT', '1')
    print(f'Running {exe}\nRomFS: {romfs}\nSaves: {userdata}\nLog: {log}', flush=True)
    start = time.monotonic()
    timed_out = False
    with log.open('x', encoding='utf-8') as output:
        with subprocess.Popen([str(exe), str(work / 'img')], cwd=exe.parent, env=env,
                              stdout=output, stderr=subprocess.STDOUT) as process:
            try:
                result = process.wait(timeout=args.seconds or None)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.kill()  # only the process created by this invocation
                process.wait()
                result = 124
            except KeyboardInterrupt:
                process.kill()
                process.wait()
                result = 130
    reason = 'time limit reached' if timed_out else f'exit {result}'
    print(f'{reason} after {time.monotonic() - start:.1f}s; log: {log}')
    return result


if __name__ == '__main__':
    raise SystemExit(main())
