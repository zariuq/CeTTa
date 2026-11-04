#!/usr/bin/env python3
"""Install the standalone embedding library, headers and language resources."""
import argparse
import os
from pathlib import Path
import shutil
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shared', type=Path, required=True)
    parser.add_argument('--static', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, default=Path('/usr/local'))
    parser.add_argument('--libdir', type=Path)
    parser.add_argument('--includedir', type=Path)
    parser.add_argument('--datadir', type=Path)
    parser.add_argument('--destdir', type=Path)
    parser.add_argument('--private-libs', default='-ldl -lm -pthread -lgmp')
    args = parser.parse_args()
    prefix = args.prefix.absolute()
    libdir = args.libdir or prefix / 'lib'
    includedir = args.includedir or prefix / 'include'
    datadir = args.datadir or prefix / 'share' / 'cetta'
    def staged(path):
        return args.destdir / path.relative_to(path.anchor) if args.destdir else path
    for path in (libdir, includedir, datadir, libdir / 'pkgconfig'):
        staged(path).mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.shared, staged(libdir / 'libcetta.so.1'))
    link = staged(libdir / 'libcetta.so')
    if link.is_symlink() or link.exists():
        link.unlink()
    link.symlink_to('libcetta.so.1')
    shutil.copy2(args.static, staged(libdir / 'libcetta.a'))
    shutil.copy2(ROOT / 'include/cetta_embed.h', staged(includedir / 'cetta_embed.h'))
    companion = staged(datadir / 'runtime/cetta-proc-scope')
    companion.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / 'runtime/cetta-proc-scope', companion)
    for name in ('lib', 'langdef'):
        for source in (ROOT / name).rglob('*'):
            if not source.is_file() or source.suffix in ('.o', '.d', '.pyc') or '__pycache__' in source.parts:
                continue
            target = staged(datadir / source.relative_to(ROOT))
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    version = tomllib.loads((ROOT / 'pyproject.toml').read_text())['project']['version']
    pc = '\n'.join([
        'prefix=${pcfiledir}/' + os.path.relpath(prefix, libdir / 'pkgconfig'),
        'libdir=${prefix}/' + os.path.relpath(libdir, prefix),
        'includedir=${prefix}/' + os.path.relpath(includedir, prefix),
        'resource_dir=${prefix}/' + os.path.relpath(datadir, prefix), '',
        'Name: CeTTa', 'Description: CeTTa PeTTa and HE embedding runtime',
        'Version: ' + version, 'Libs: -L${libdir} -lcetta',
        'Libs.private: ' + args.private_libs, 'Cflags: -I${includedir}', '',
    ])
    staged(libdir / 'pkgconfig/cetta.pc').write_text(pc)
    print('Installed CeTTa C ABI and language resources.')


if __name__ == '__main__':
    main()
