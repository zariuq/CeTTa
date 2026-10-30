"""Source candidates, provenance and reproducible compiler caching."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from cesta_source import identity


def run(arguments, success=True, env=None):
    result = subprocess.run(list(map(str, arguments)), cwd=ROOT, env=env, capture_output=True, text=True)
    assert (result.returncode == 0) == success, (arguments, result.returncode, result.stdout, result.stderr)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--build', action='store_true')
    args = parser.parse_args()
    args.source = args.source.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='toolchain-', dir=args.output.resolve()))
    candidates = [work / 'first/source', work / 'elsewhere/source']
    for candidate in candidates:
        run([sys.executable, 'tools/cesta_vendor.py', 'prepare', args.source, 'HEAD', candidate, '--working-tree'])
    source_id = identity(candidates[0])
    assert identity(candidates[1]) == source_id
    metadata = json.loads((candidates[0] / 'SOURCE.json').read_text())
    assert metadata['snapshot'] == 'working-tree' and metadata['source_sha256'] == source_id
    assert not list(candidates[0].rglob('*.md'))
    # Metadata and destination paths do not change a content identity.
    (candidates[1] / 'COMMIT').write_text('different provenance text\n')
    assert identity(candidates[1]) == source_id
    (candidates[1] / 'src/project.c').write_text((candidates[1] / 'src/project.c').read_text() + '\n/* changed */\n')
    assert identity(candidates[1]) != source_id
    run([sys.executable, 'tools/cesta_vendor.py', 'promote', candidates[1], work / 'bad', '--previous', work / 'previous'], False)
    current = work / 'current'
    current.mkdir(); (current / 'Makefile').write_text('all:\n\t@true\n')
    (current / 'sentinel.c').write_text('int preserved;\n')
    run([sys.executable, 'tools/cesta_vendor.py', 'promote', candidates[0], current, '--previous', work / 'previous'])
    assert identity(current) == source_id
    assert len(list((work / 'previous').glob('*/sentinel.c'))) == 1
    if args.build:
        # The build checks the same snapshot in independent directories.
        other = work / 'third/source'
        run([sys.executable, 'tools/cesta_vendor.py', 'prepare', args.source, 'HEAD', other, '--working-tree'])
        binaries = [work / 'a/tool', work / 'b/tool']
        env = dict(os.environ, CESTA_NATIVE_CC=args.cc, CESTA_TOOL_JOBS='1')
        for source, binary in zip((current, other), binaries):
            run(['tools/cesta_tool.sh', 'build', source, binary], env=env)
        assert binaries[0].read_bytes() == binaries[1].read_bytes()
        before = binaries[0].stat().st_mtime_ns
        run(['tools/cesta_tool.sh', 'build', current, binaries[0]], env=env)
        assert binaries[0].stat().st_mtime_ns == before
        digest = hashlib.sha256(binaries[0].read_bytes()).hexdigest()
    else:
        digest = None
    (work / 'receipt.json').write_text(json.dumps(dict(source_sha256=source_id, reproducible_binary=digest,
                                                     prior_source_preserved=True, tampering_refused=True), indent=2) + '\n')
    print('PASS: content identity, explicit working provenance, tamper refusal, preserved prior bundle' +
          (' and reproducible compiler builds' if args.build else ''))


if __name__ == '__main__':
    main()
