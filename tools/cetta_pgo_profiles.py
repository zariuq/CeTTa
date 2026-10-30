"""Move one run's collected profiles into its profile-using namespace."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys


def main():
    generated, used, receipt = sys.argv[1:]
    rows = []
    for directory, dirs, names in os.walk('.'):
        dirs[:] = [d for d in dirs if d not in ('.git', 'vendor')]
        for name in sorted(names):
            if generated not in name or not name.endswith('.gcda'):
                continue
            source = Path(directory) / name
            destination = source.with_name(name.replace(generated, used))
            if destination == source or destination.exists():
                raise SystemExit(f'profile namespace collision: {destination}')
            shutil.copy2(source, destination)
            rows.append(dict(source=str(source), destination=str(destination),
                             sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
    if not rows:
        raise SystemExit('training wrote no profiles for the instrumented namespace')
    Path(receipt).write_text(json.dumps(dict(generated=generated, used=used, profiles=rows), indent=2) + '\n')
    print(f'PGO: {len(rows)} profiles transferred without modifying earlier runs')


if __name__ == '__main__':
    main()
