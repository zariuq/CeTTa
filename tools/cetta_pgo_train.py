"""Qualify declared PGO training against a non-instrumented reference."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def workloads(path, root):
    document = json.loads(path.read_text())
    if document.get('format') != 'CettaPGOWorkloadsV1':
        raise ValueError('expected CettaPGOWorkloadsV1')
    seen_paths, seen_contents = {}, {}
    for part in ('training', 'held_out'):
        if not document.get(part):
            raise ValueError(f'empty {part} set')
        for row in document[part]:
            path = (root / row['program']).resolve()
            path.relative_to(root.resolve())
            if row['language'] not in ('prime', 'he', 'petta'):
                raise ValueError('unknown workload language')
            repeat = row.get('repeat', 1)
            if not isinstance(repeat, int) or isinstance(repeat, bool) or repeat < 1:
                raise ValueError('repeat must be a positive integer')
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if path in seen_paths or (digest in seen_contents and seen_contents[digest] != part):
                raise ValueError(f'overlapping or duplicated workload: {path}')
            seen_paths[path] = part
            seen_contents[digest] = part
            row['source_sha256'] = digest
    return document


def execute(binary, row, root, timeout):
    if hashlib.sha256((root / row['program']).read_bytes()).hexdigest() != row['source_sha256']:
        raise ValueError(f'workload changed during qualification: {row["program"]}')
    command = [str(binary.resolve()), '--lang', row['language'], row['program']]
    result = subprocess.run(command, cwd=root, stdin=subprocess.DEVNULL, capture_output=True, timeout=timeout)
    if result.returncode:
        raise ValueError(f'training failed: {row["program"]}: status {result.returncode}: {result.stderr[-1500:]!r}')
    if hashlib.sha256((root / row['program']).read_bytes()).hexdigest() != row['source_sha256']:
        raise ValueError(f'workload changed during qualification: {row["program"]}')
    return hashlib.sha256(result.stdout).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--timeout', type=float, default=60)
    args = parser.parse_args()
    try:
        document = workloads(args.manifest, args.root)
        if not args.binary:
            print(f'PGO workloads: {len(document["training"])} training, {len(document["held_out"])} held out; no overlap')
            return
        if not args.reference or not args.output:
            parser.error('training needs --reference and --output')
        rows = []
        for row in document['training']:
            reference = execute(args.reference, row, args.root, args.timeout)
            start = time.monotonic()
            for _ in range(row.get('repeat', 1)):
                actual = execute(args.binary, row, args.root, args.timeout)
                if actual != reference:
                    raise ValueError(f'instrumentation changed output: {row["program"]}')
            rows.append(dict(row, output_sha256=reference, seconds=time.monotonic()-start))
            print(f'PGO trained {row["language"]}: {row["program"]} x{row.get("repeat", 1)}', flush=True)
        args.output.write_text(json.dumps(dict(format='CettaPGOTrainingV1',
            manifest_sha256=hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
            instrumented_sha256=hashlib.sha256(args.binary.read_bytes()).hexdigest(),
            reference_sha256=hashlib.sha256(args.reference.read_bytes()).hexdigest(),
            training=rows, held_out=document['held_out']), indent=2) + '\n')
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))


if __name__ == '__main__':
    main()
