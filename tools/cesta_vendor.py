"""Prepare and promote content-identified Cesta sources without changing Git."""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import time

from cesta_source import OMIT, METADATA, files, identity


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])


def permitted(name):
    path = PurePosixPath(name)
    return not path.is_absolute() and '..' not in path.parts and not any(p in OMIT for p in path.parts) \
        and path.name not in METADATA and path.suffix not in ('.md', '.pyc')


def prepare(repo, revision, destination, working):
    if destination.exists():
        raise ValueError(f'{destination} already exists; prepare a new candidate')
    commit = git(repo, 'rev-parse', '--verify', revision + '^{commit}').decode().strip()
    if working and commit != git(repo, 'rev-parse', 'HEAD').decode().strip():
        raise ValueError('a working-tree snapshot must be based on HEAD')
    destination.mkdir(parents=True)
    if working:
        names = git(repo, 'ls-files', '-z', '--cached', '--others', '--exclude-standard').decode().split('\0')
        for name in sorted(set(names)):
            if not name or not permitted(name):
                continue
            source = repo / name
            if source.is_symlink():
                raise ValueError(f'symlink in source bundle: {name}')
            if not source.exists():
                continue  # A tracked deletion is part of the working snapshot.
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    else:
        with tarfile.open(fileobj=io.BytesIO(git(repo, 'archive', commit))) as archive:
            for entry in archive:
                if not entry.isfile() or not permitted(entry.name):
                    continue
                target = destination / entry.name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(entry).read())
                target.chmod(entry.mode & 0o777)
    content = identity(destination)
    manifest = {path.relative_to(destination).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in files(destination)}
    metadata = dict(format='CestaSourceV1', base_commit=commit,
                    snapshot='working-tree' if working else 'commit', source_sha256=content, files=manifest)
    (destination / 'SOURCE.json').write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n')
    (destination / 'COMMIT').write_text(commit + '\n' + metadata['snapshot'] + '\n')
    print(f'prepared Cesta {content[:20]} ({metadata["snapshot"]}, base {commit[:12]})')


def promote(candidate, destination, previous):
    metadata = json.loads((candidate / 'SOURCE.json').read_text())
    if identity(candidate) != metadata['source_sha256']:
        raise ValueError('candidate source changed after preparation')
    if destination.exists():
        previous.mkdir(parents=True, exist_ok=True)
        retained = previous / f'{identity(destination)[:20]}-{time.time_ns()}'
        destination.rename(retained)
        try:
            candidate.rename(destination)
        except BaseException:
            retained.rename(destination)
            raise
        print(f'previous Cesta source retained as {retained}')
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        candidate.rename(destination)
    print(f'promoted qualified Cesta {metadata["source_sha256"][:20]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    prepare_parser = sub.add_parser('prepare')
    prepare_parser.add_argument('repo', type=Path)
    prepare_parser.add_argument('revision')
    prepare_parser.add_argument('destination', type=Path)
    prepare_parser.add_argument('--working-tree', action='store_true')
    promote_parser = sub.add_parser('promote')
    promote_parser.add_argument('candidate', type=Path)
    promote_parser.add_argument('destination', type=Path)
    promote_parser.add_argument('--previous', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.action == 'prepare':
            prepare(args.repo.resolve(), args.revision, args.destination, args.working_tree)
        else:
            promote(args.candidate, args.destination, args.previous)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))


if __name__ == '__main__':
    main()
