#!/usr/bin/env python3
"""Combine native objects and keep the public embedding ABI global."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--ar', default='ar')
    parser.add_argument('--nm', default='nm')
    parser.add_argument('--objcopy', default='objcopy')
    parser.add_argument('--header', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--allocator', type=Path, required=True)
    parser.add_argument('objects', nargs='+')
    args = parser.parse_args()
    public = set(re.findall(r'\b(cetta_embed_[a-zA-Z0-9_]+)\s*\(', args.header.read_text()))
    with tempfile.TemporaryDirectory(prefix='.cetta-static-', dir=args.output.parent) as temporary:
        directory = Path(temporary)
        combined, archive = directory / 'runtime.o', directory / 'libcetta.a'
        core = directory / 'core.o'
        objects = [name for name in args.objects if Path(name).resolve() != args.allocator.resolve()]
        subprocess.run(shlex.split(args.cc) + ['-nostdlib', '-no-pie', '-r', '-o', str(core)]
                       + objects, check=True)
        # Bind only this copy of the native core to its allocation owner.
        # The allocator itself and foreign libraries retain libc allocation.
        rename = []
        for name in ('malloc', 'calloc', 'realloc', 'free', 'strdup', 'strndup'):
            rename += ['--redefine-sym', name + '=cetta_native_' + name]
        subprocess.run(shlex.split(args.objcopy) + rename + [str(core)], check=True)
        subprocess.run(shlex.split(args.cc) + ['-nostdlib', '-no-pie', '-r', '-o', str(combined),
                                               str(core), str(args.allocator)], check=True)
        symbols = subprocess.check_output(shlex.split(args.nm) +
            ['-P', '--defined-only', '--extern-only', str(combined)], text=True)
        defined = {line.split()[0] for line in symbols.splitlines() if line.strip()}
        if public - defined:
            raise RuntimeError(f'public functions missing: {sorted(public - defined)}')
        private = directory / 'private-symbols'
        private.write_text('\n'.join(sorted(defined - public)) + '\n')
        subprocess.run(shlex.split(args.objcopy) + ['--localize-symbols=' + str(private), str(combined)], check=True)
        subprocess.run(shlex.split(args.ar) + ['rcs', str(archive), str(combined)], check=True)
        archive.replace(args.output)


if __name__ == '__main__':
    main()
