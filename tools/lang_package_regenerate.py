#!/usr/bin/env python3
"""Regenerate the language package files from their recorded sources.

For each file in langdef/lang/manifest.json, produce it again from the
Mettapedia checkout it names (a copy, or a run of a Lean emitter with that
checkout's Lean toolchain) and compare it with the shipped copy.  This is
provenance tooling outside the tests; make test-lang-package-manifest checks
the shipped files against their recorded hashes and needs no checkout.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from lang_package_manifest import MANIFEST


def regenerate(entry, mettapedia, output):
    """Produce the file an entry describes into OUTPUT, from the Mettapedia checkout."""
    source = entry['source']
    if source['kind'] == 'copy':
        output.write_bytes((mettapedia / source['path']).read_bytes())
    elif source['kind'] == 'lean-run':
        lean_root = mettapedia / 'lean/mettapedia'
        subprocess.run(['lake', 'build', source['module']], cwd=lean_root, check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['lake', 'env', 'lean', '--run', str(mettapedia / source['path']), str(output)],
                       cwd=lean_root, check=True, stdout=subprocess.DEVNULL)
    else:
        raise SystemExit(f'unknown source kind: {source["kind"]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--mettapedia', type=Path, required=True, help='Mettapedia checkout root')
    args = parser.parse_args()
    files = json.loads(MANIFEST.read_text())['files']
    mettapedia = args.mettapedia.resolve(strict=True)
    failures = []
    with tempfile.TemporaryDirectory() as work:
        for name, entry in files.items():
            output = Path(work) / Path(name).name
            regenerate(entry, mettapedia, output)
            if output.read_bytes() != (MANIFEST.parent / name).read_bytes():
                failures.append(f'{name}: differs from its regenerated source')
    for failure in failures:
        print('FAIL ' + failure)
    print(f'(LangPackageRegeneration {len(files)} files regenerated, {len(failures)} failures)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
