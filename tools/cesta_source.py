"""Content identity for a self-contained Cesta source bundle."""
import hashlib
import os
from pathlib import Path
import subprocess
import shutil
import sys

OMIT = {'.git', 'build', 'tests', 'examples', '__pycache__'}
METADATA = {'COMMIT', 'SOURCE.json'}


def files(root):
    root = Path(root)
    selected = []
    for directory, dirs, names in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in OMIT and not (Path(directory) / d).is_symlink())
        for name in names:
            path = Path(directory) / name
            if name not in METADATA and path.suffix not in ('.md', '.pyc') and not path.is_symlink():
                selected.append(path)
    return sorted(selected)


def identity(root):
    root = Path(root)
    if not (root / 'Makefile').is_file():
        raise ValueError(f'{root}: not a Cesta source tree')
    digest = hashlib.sha256()
    for path in files(root):
        digest.update(path.relative_to(root).as_posix().encode() + b'\0')
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def tool_identity(root, compiler):
    digest = hashlib.sha256(identity(root).encode())
    # The source identity and build recipe are separate from the compiler.
    digest.update(b'cesta-tool/2:-O2,-g,-ffile-prefix-map,CPPFLAGS=')
    for name in ('cesta_tool.sh', 'cesta_source.py'):
        digest.update(hashlib.sha256(Path(__file__).with_name(name).read_bytes()).digest())
    for flags in (['--version'], ['-dumpmachine']):
        digest.update(subprocess.check_output([compiler, *flags], timeout=15))
    executable = shutil.which(compiler)
    if executable:
        digest.update(hashlib.sha256(Path(executable).read_bytes()).digest())
    return digest.hexdigest()


if __name__ == '__main__':
    try:
        value = tool_identity(sys.argv[2], sys.argv[3]) if sys.argv[1] == 'tool-id' else identity(sys.argv[2])
        print(value[:20])
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
