#!/usr/bin/env python3
"""Preview or remove expired generated binaries while preserving source and evidence.

Nothing is removed without --apply. A JSONL manifest records each removed
artifact's digest. Tracked files, pinned files, live executable/mapped files,
symlinks, nested repositories and the selected build tags are excluded.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from hashlib import sha256
import json
import math
import os
from pathlib import Path
import stat
import subprocess
import sys
import time


@dataclass(frozen=True)
class Artifact:
    path: Path
    relative: str
    identity: tuple[int, int, int, int]
    blocks: int
    links: int
    reason: str


def identity(info):
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns


def tracked_files(root):
    result = subprocess.run(['git', '-C', str(root), 'ls-files', '-z'],
                            check=True, capture_output=True)
    return {os.fsdecode(name) for name in result.stdout.split(b'\0') if name}


def ancestors():
    result = set()
    pid = os.getpid()
    while pid > 1 and pid not in result:
        result.add(pid)
        try:
            fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
            pid = int(fields[1])
        except (OSError, ValueError, IndexError):
            break
    return result


def active_files(root):
    """Fail before pruning a tree with an active compiler or build driver."""
    held = set()
    own = ancestors()
    builders = {'make', 'gmake', 'gcc', 'g++', 'cc', 'cc1', 'cc1plus', 'clang',
                'clang++', 'ld', 'ld.bfd', 'ld.lld', 'collect2', 'lake', 'lean'}
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            if process.stat().st_uid != os.getuid():
                continue
            cwd = (process / 'cwd').resolve(strict=True)
            command = (process / 'comm').read_text().strip()
            if int(process.name) not in own and cwd.is_relative_to(root) and command in builders:
                raise RuntimeError(f'active build in this tree: PID {process.name} ({command})')
            paths = [process / 'exe']
            try:
                paths.extend((process / 'fd').iterdir())
            except OSError:
                pass
            for path in paths:
                try:
                    info = path.stat()
                    if stat.S_ISREG(info.st_mode):
                        held.add((info.st_dev, info.st_ino))
                except OSError:
                    pass
            for line in (process / 'maps').read_text().splitlines():
                columns = line.split(None, 5)
                if len(columns) < 6 or not columns[5].startswith('/'):
                    continue
                try:
                    info = Path(columns[5].removesuffix(' (deleted)')).stat()
                    held.add((info.st_dev, info.st_ino))
                except OSError:
                    pass
        except (OSError, ValueError):
            continue
    return held


def kind(path, legacy):
    if path.suffix in {'.c', '.h', '.inc', '.lean', '.metta', '.md', '.json', '.jsonl',
                       '.txt', '.log', '.stdout', '.stderr', '.py', '.sh', '.nul',
                       '.ns', '.tex', '.mm', '.mmb'}:
        return None
    try:
        with path.open('rb') as stream:
            magic = stream.read(8)
    except OSError:
        return None
    if magic.startswith(b'\x7fELF') or magic == b'!<arch>\n':
        return 'compiled binary'
    if legacy and (path.name.endswith(('.olean', '.olean.private', '.olean.server', '.ilean'))):
        if magic.startswith(b'olean') or '.lake' in path.parts or path.with_suffix('.lean').exists():
            return 'compiled Lean module'
    return None


def walk(root, folder):
    for current, directories, files in os.walk(folder, followlinks=False):
        base = Path(current)
        if base != root and (base / '.git').exists():
            directories[:] = []
            continue
        directories[:] = [name for name in directories
                          if name != '.git' and not (base / name).is_symlink()]
        for name in files:
            path = base / name
            if not path.is_symlink():
                yield path


def candidates(root, age_days, keep, keep_tags, legacy_work, held):
    tracked = tracked_files(root)
    cutoff = time.time() - age_days * 86400
    scopes = [(root / 'runtime', False)]
    if legacy_work:
        scopes.append((root / '.codex-work', True))
    result = []
    seen = set()
    for folder, legacy in scopes:
        if not folder.is_dir() or folder.is_symlink():
            continue
        for path in walk(root, folder):
            relative = str(path.relative_to(root))
            if relative in seen or relative in tracked or relative in keep:
                continue
            seen.add(relative)
            try:
                info = path.lstat()
            except OSError:
                continue
            if not stat.S_ISREG(info.st_mode) or (info.st_dev, info.st_ino) in held:
                continue
            if info.st_mtime >= cutoff:
                continue
            if not legacy and any(tag in path.name for tag in keep_tags):
                continue
            reason = kind(path, legacy)
            if reason:
                result.append(Artifact(path, relative, identity(info), info.st_blocks * 512,
                                       info.st_nlink, reason))
    return result


def prune(root, artifacts, manifest, apply, held):
    removed = changed = logical = released = 0
    counts = Counter()
    hashes = {}
    with manifest.open('x', encoding='utf-8') as log:
        log.write(json.dumps({'schema': 'cetta-build-pruning-v1', 'apply': apply,
                              'created': time.time(), 'candidates': len(artifacts)}) + '\n')
        for item in artifacts:
            try:
                # Recheck parents too: a replaced directory cannot redirect deletion.
                if item.path.resolve(strict=True) != item.path or identity(item.path.lstat()) != item.identity:
                    changed += 1
                    continue
                key = item.identity[:2]
                if key in held:
                    changed += 1
                    continue
                digest = hashes.get(key)
                if apply and digest is None:
                    with item.path.open('rb') as stream:
                        digest = sha256()
                        while chunk := stream.read(1024 * 1024):
                            digest.update(chunk)
                        digest = digest.hexdigest()
                    hashes[key] = digest
                if identity(item.path.lstat()) != item.identity:
                    changed += 1
                    continue
                record = {'path': item.relative, 'sha256': digest, 'bytes': item.identity[2],
                          'reason': item.reason, 'removed': apply}
                log.write(json.dumps(record, ensure_ascii=True) + '\n')
                log.flush()
                if apply:
                    info = item.path.stat()
                    item.path.unlink()
                    if info.st_nlink == 1:
                        released += info.st_blocks * 512
                removed += 1
                logical += item.identity[2]
                counts[item.reason] += 1
            except FileNotFoundError:
                changed += 1
    return {'removed' if apply else 'candidates': removed, 'logical_bytes': logical,
            'released_file_blocks': released, 'changed_or_busy': changed, 'kinds': dict(counts)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--age-days', type=float, default=7)
    parser.add_argument('--keep', action='append', default=[])
    parser.add_argument('--keep-tag', action='append', default=[])
    parser.add_argument('--keep-file', type=Path, default=Path('runtime/prune-keep.txt'))
    parser.add_argument('--legacy-work', action='store_true', help='include retired .codex-work build artifacts')
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    actual = subprocess.check_output(['git', '-C', str(root), 'rev-parse', '--show-toplevel'], text=True).strip()
    if Path(actual).resolve() != root or not math.isfinite(args.age_days) or args.age_days < 0 or any(not tag for tag in args.keep_tag):
        parser.error('use the repository root, a nonnegative age and nonempty keep tags')
    keep = set(args.keep)
    pins = args.keep_file if args.keep_file.is_absolute() else root / args.keep_file
    if pins.is_file():
        keep.update(line.strip() for line in pins.read_text().splitlines()
                    if line.strip() and not line.lstrip().startswith('#'))
    normalized = set()
    for value in keep:
        path = Path(value)
        path = path if path.is_absolute() else root / path
        if not path.resolve().is_relative_to(root):
            parser.error('pinned artifacts must belong to this repository')
        normalized.add(str(path.resolve().relative_to(root)))
    keep = normalized
    for folder in (root, root / 'runtime'):
        if folder.is_dir():
            for path in folder.iterdir():
                if path.is_symlink():
                    resolved = path.resolve()
                    if resolved.is_relative_to(root):
                        keep.add(str(resolved.relative_to(root)))
    held = active_files(root)
    artifacts = candidates(root, args.age_days, keep, args.keep_tag, args.legacy_work, held)
    held |= active_files(root)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    print(json.dumps(prune(root, artifacts, args.manifest, args.apply, held), sort_keys=True))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f'pruning refused: {error}', file=sys.stderr)
        raise SystemExit(1)
