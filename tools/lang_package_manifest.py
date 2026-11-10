#!/usr/bin/env python3
"""Check the provenance of the generated and copied language package files.

langdef/lang/manifest.json records, for each file that CeTTa ships but does
not author, where it comes from and its SHA-256.

  check                  the files match their recorded hashes (no Mettapedia)
  check --regenerate     also regenerate the files from a Mettapedia checkout
                         and compare them with the shipped copies
  update                 record the hashes of the files as they are now

Regeneration needs Mettapedia and its Lean toolchain; using the packages does
not.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ENGINE = Path(__file__).resolve().parents[1]
MANIFEST = ENGINE / 'langdef/lang/manifest.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


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
    parser.add_argument('action', choices=['check', 'update'])
    parser.add_argument('--regenerate', action='store_true')
    parser.add_argument('--mettapedia', type=Path, help='Mettapedia checkout root (for --regenerate)')
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    files = manifest['files']
    if args.action == 'update':
        for name, entry in files.items():
            entry['sha256'] = digest(MANIFEST.parent / name)
        MANIFEST.write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'recorded {len(files)} file hashes')
        return 0
    failures = []
    for name, entry in files.items():
        if digest(MANIFEST.parent / name) != entry['sha256']:
            failures.append(f'{name}: differs from its recorded hash')
    if args.regenerate:
        if args.mettapedia is None:
            raise SystemExit('--regenerate needs --mettapedia')
        mettapedia = args.mettapedia.resolve(strict=True)
        with tempfile.TemporaryDirectory() as work:
            for name, entry in files.items():
                output = Path(work) / Path(name).name
                regenerate(entry, mettapedia, output)
                if output.read_bytes() != (MANIFEST.parent / name).read_bytes():
                    failures.append(f'{name}: differs from its regenerated source')
    for failure in failures:
        print('FAIL ' + failure)
    checked = 'regenerated and hashed' if args.regenerate else 'hashed'
    print(f'(LangPackageManifest {len(files)} files {checked}, {len(failures)} failures)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
