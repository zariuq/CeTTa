#!/usr/bin/env python3
"""Persistent ACT admission stages one row at a time and publishes atomically."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import os
import sys
import shlex
import time

ROOT = Path(__file__).resolve().parents[2]


def scale(binary, fixture, directory, rows, payload, timeout):
    directory.mkdir(mode=0o700)
    image = directory/'input.cact'
    subprocess.run([str(fixture), 'generate', str(image), str(rows), str(payload)],
                   capture_output=True, text=True, check=True, timeout=timeout)
    source = directory/'client.metta'
    source.write_text('!(import! &self ' + json.dumps(str(ROOT/'tests/support/space_append_fixture.metta')) + ')\n'
                      + '!(println! (append-fixture:run (Scale '
                      + json.dumps(str(directory/'store')) + ' ' + json.dumps(str(image)) + ' ' + str(rows) + ')))\n')
    peak = {key: 0 for key in ['VmRSS', 'RssAnon', 'RssFile', 'VmHWM']}
    samples = 0
    began = time.monotonic()
    with open(directory/'run.log', 'w') as log:
        process = subprocess.Popen([str(binary), '--lang', 'petta', str(source)], stdout=log, stderr=subprocess.STDOUT)
        while process.poll() is None:
            try:
                fields = dict(line.split(':', 1) for line in (Path('/proc')/str(process.pid)/'status').read_text().splitlines() if ':' in line)
                for key in peak:
                    peak[key] = max(peak[key], int(fields.get(key, '0').split()[0]))
                samples += 1
            except (FileNotFoundError, ProcessLookupError):
                pass
            if time.monotonic()-began > timeout:
                process.kill()
                process.wait()
                raise AssertionError(('scaling deadline', rows))
            time.sleep(.02)
    output = (directory/'run.log').read_text()
    assert process.returncode == 0 and 'APPEND_SCALE_OK' in output, (rows, process.returncode, output)
    disk = sum(path.stat().st_size for path in (directory/'store').iterdir() if path.is_file())
    return dict(rows=rows, input_bytes=image.stat().st_size, store_bytes=disk,
                seconds=time.monotonic()-began, peak_kib=peak if samples else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('act_fixture', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--scale-rows', type=int, nargs='*', default=[])
    parser.add_argument('--scale-payload', type=int, default=512)
    parser.add_argument('--scale-timeout', type=float, default=300)
    args = parser.parse_args()
    results = []
    with tempfile.TemporaryDirectory(prefix='space-append-', dir=ROOT/'runtime') as temporary:
        root = Path(temporary)
        image = root/'input.cact'
        subprocess.run([str(args.act_fixture.resolve()), 'write', str(image)], capture_output=True, check=True)
        for case in ['Basic', 'Retain', 'Cancel', 'Stale', 'Program', 'Premature', 'Collision', 'Legacy', 'Indexed']:
            directory = root/case
            directory.mkdir(mode=0o700)
            source = directory/'client.metta'
            source.write_text('!(import! &self ' + json.dumps(str(ROOT/'tests/support/space_append_fixture.metta')) + ')\n'
                              + '!(println! (append-fixture:run (' + case + ' '
                              + json.dumps(str(directory/'store')) + ' ' + json.dumps(str(image)) + ')))\n')
            run = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', str(source)],
                                 capture_output=True, text=True, timeout=40)
            marker = 'APPEND_' + case.upper() + '_OK'
            assert run.returncode == 0 and marker in run.stdout.splitlines(), (
                case, run.returncode, run.stdout, run.stderr)
            assert 'Assertion failed' not in run.stdout + run.stderr
            results.append(dict(case=case, marker=marker, exit=run.returncode))
        if sys.platform.startswith('linux'):
            library = root/'append-fault.so'
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-shared', '-fPIC',
                           str(ROOT/'tests/support/append_io_fault.c'), '-ldl', '-o', str(library)], check=True)
            for point in [1, 10, 100]:
                directory = root/('fault-'+str(point))
                directory.mkdir(mode=0o700)
                marker_file = directory/'injected'
                source = directory/'client.metta'
                source.write_text('!(import! &self ' + json.dumps(str(ROOT/'tests/support/space_append_fixture.metta')) + ')\n'
                                  + '!(println! (append-fixture:run (Failure '
                                  + json.dumps(str(directory/'store')) + ' ' + json.dumps(str(image)) + ')))\n')
                run = subprocess.run([str(args.binary.resolve()), '--lang', 'petta', str(source)],
                                     env=dict(os.environ, LD_PRELOAD=str(library), CETTA_APPEND_FAULT_AT=str(point),
                                              CETTA_APPEND_FAULT_MARKER=str(marker_file)),
                                     capture_output=True, text=True, timeout=40)
                assert marker_file.exists(), ('fault not exercised', point, run.stdout, run.stderr)
                assert run.returncode == 0 and 'APPEND_FAILURE_OK' in run.stdout.splitlines(), (
                    point, run.returncode, run.stdout, run.stderr)
                results.append(dict(case='IOFailure-'+str(point), marker='APPEND_FAILURE_OK', exit=0))
        measurements = [scale(args.binary.resolve(), args.act_fixture.resolve(), root/('scale-'+str(rows)),
                              rows, args.scale_payload, args.scale_timeout) for rows in args.scale_rows]
    report = dict(results=results, scaling=measurements, status='pass')
    if args.output:
        args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
