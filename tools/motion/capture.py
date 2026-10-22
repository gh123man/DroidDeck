#!/usr/bin/env python3
"""Arm/export app-owned diagnostics over ADB; does not execute guest commands."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import time

PACKAGE = 'com.droiddeck.launcher.debug'
ROOT = 'files/.wayland-rt'


def adb(*args, data=None):
    command = [os.environ.get('ADB', 'adb')]
    if os.environ.get('ANDROID_SERIAL'):
        command += ['-s', os.environ['ANDROID_SERIAL']]
    return subprocess.run(command + list(args), input=data, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=True, timeout=15).stdout


def shell(*args, data=None):
    return adb('shell', shlex.join(['run-as', PACKAGE, *args]), data=data)


def statuses():
    result = {}
    for name in shell('ls', ROOT).decode().splitlines():
        if not re.fullmatch(r'motion-(android|driver)-\d+\.status', name): continue
        text = shell('cat', f'{ROOT}/{name}').decode()
        values = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', text)}
        process = name.split('-')[1]
        values['file'] = name
        if values.get('now_ns', 0) > result.get(process, {}).get('now_ns', 0): result[process] = values
    return result


def arm(mode, trace, seconds, delay, pixels=False):
    before = statuses(); time.sleep(.5); current = statuses()
    for process in ('android', 'driver'):
        if process not in current or current[process]['now_ns'] <= before.get(process, {}).get('now_ns', 0):
            raise RuntimeError(f'{process} diagnostic controller is absent or not advancing')
        if current[process]['active']:
            raise RuntimeError(f'{process} still has an active capture; wait for its export')
    clocks = [s['now_ns'] for s in current.values()]
    if max(clocks)-min(clocks) > 2_000_000_000:
        raise RuntimeError('controller clocks disagree; do not arm')
    run = max(time.time_ns()//1_000_000, max(s['run'] for s in current.values())+1)
    epoch = max(clocks)+int(delay*1e9)
    if pixels: shell('touch', f'{ROOT}/motion-pixels')
    else: shell('rm', '-f', f'{ROOT}/motion-pixels')
    command = f'{run} {mode} {int(trace)} {epoch} {round(seconds*1000)}\n'.encode()
    shell('sh', '-c', f'cat > {ROOT}/motion-control.tmp && mv {ROOT}/motion-control.tmp {ROOT}/motion-control', data=command)
    for _ in range(20):
        time.sleep(.2); current = statuses()
        if all(current.get(p, {}).get('run') == run for p in ('android','driver')):
            return dict(run=run, mode=mode, trace=trace, pixels=pixels, start_ns=epoch,
                        duration_seconds=seconds, return_to_live='automatic after capture', controllers=current)
    raise RuntimeError(f'run {run} was not acknowledged by both controllers; inspect status before testing')


def collect(run, output):
    current = statuses()
    if not all(current.get(p, {}).get('run') == run and current[p]['exported'] == 1 for p in ('android','driver')):
        raise RuntimeError('both controllers must report this run exported successfully before collection')
    output.mkdir(parents=True, exist_ok=True)
    files = []
    for name in shell('ls', ROOT).decode().splitlines():
        if not re.fullmatch(rf'motion-{run}-(android|driver)-\d+-\d+\.bin', name): continue
        content = shell('cat', f'{ROOT}/{name}')
        (output/name).write_bytes(content); files.append(name)
    for name in shell('ls', ROOT).decode().splitlines():
        if re.fullmatch(rf'motion-{run}-driver-\d+\.pixels', name):
            (output/name).write_bytes(shell('cat', f'{ROOT}/{name}'))
    (output/'controllers.json').write_text(json.dumps(current, indent=2)+'\n')
    if len(files) != 16: raise RuntimeError(f'expected 16 streams (including empty streams), got {len(files)}')
    return dict(run=run, output=str(output.resolve()), files=files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('status')
    commands.add_parser('enable')
    commands.add_parser('disable')
    a = commands.add_parser('arm')
    a.add_argument('--mode', choices=('live','fixed','sine'), default='live')
    a.add_argument('--trace', choices=('on','off'), default='on')
    a.add_argument('--seconds', type=float, default=25)
    a.add_argument('--delay', type=float, default=10)
    a.add_argument('--pixels', action='store_true', help='Also capture actual left-eye pixels at the source-copy boundary')
    c = commands.add_parser('collect'); c.add_argument('run', type=int); c.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.command == 'enable':
        shell('mkdir', '-p', ROOT)
        shell('touch', f'{ROOT}/motion-enabled')
        result = dict(enabled=True, next='Restart the XR Dev app and SteamVR to start the diagnostic controllers.')
    elif args.command == 'disable':
        shell('rm', '-f', f'{ROOT}/motion-enabled', f'{ROOT}/motion-control', f'{ROOT}/motion-pixels')
        result = dict(enabled=False, next='Restart the XR Dev app and SteamVR to stop the controllers and reclaim buffers.')
    elif args.command == 'status': result = statuses()
    elif args.command == 'arm':
        if not .1 <= args.seconds <= 30 or not 3 <= args.delay <= 120: parser.error('seconds must be 0.1–30; delay 3–120')
        result = arm(('live','fixed','sine').index(args.mode), args.trace == 'on', args.seconds, args.delay, args.pixels)
    else: result = collect(args.run, args.output)
    print(json.dumps(result, indent=2))

if __name__ == '__main__': main()
