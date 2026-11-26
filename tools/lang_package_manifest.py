#!/usr/bin/env python3
"""Check the generated and copied language package files against their hashes.

langdef/lang/manifest.json records, for each file that CeTTa ships but does
not author, where it comes from and its SHA-256.

  check                  the files match their recorded hashes
  update                 record the hashes of the files as they are now

tools/lang_package_regenerate.py regenerates the files from their recorded
sources and compares them with the shipped copies (outside the tests).
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ENGINE = Path(__file__).resolve().parents[1]
MANIFEST = ENGINE / 'langdef/lang/manifest.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('action', choices=['check', 'update'])
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
    for failure in failures:
        print('FAIL ' + failure)
    print(f'(LangPackageManifest {len(files)} files hashed, {len(failures)} failures)')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
