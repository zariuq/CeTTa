#!/usr/bin/env python3
"""Qualify Python byte-order export against the retained native library."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import sysconfig
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cc", "cppflags", "cflags", "ldflags", "library"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    flags = shlex.split(args.cppflags) + shlex.split(args.cflags)
    flags += ["-Iinclude", "-Ipython", "-I" + sysconfig.get_path("include")]
    link = shlex.split(args.ldflags) + ["-Wl,--version-script=python/exports.map", "-Wl,--exclude-libs,ALL"]
    probe = '''import numpy as np
from cetta import Engine
order = ">" if np.little_endian else "<"
source = np.frombuffer(np.asarray([1, 2, 3], dtype=order + "i8").tobytes(), dtype=order + "i8")
with Engine() as engine:
    result = engine.tensor(source, copy=False).to_numpy(copy=False)
    assert result.dtype == source.dtype, "foreign byte order disappeared from Python export"
    assert np.shares_memory(source, result), "immutable foreign storage was copied"
    np.testing.assert_array_equal(source, result)
'''
    before = "size_t prefix = width > 1 && buffer->info.byte_order != CETTA_BYTE_ORDER_NATIVE ? 1u : 0u;"
    buffers = Path("python/buffers.inc").read_text()
    source = Path("python/native.c").read_text()
    include = '#include "buffers.inc"'
    if buffers.count(before) != 1 or source.count(include) != 1:
        raise RuntimeError("expected one Python format anchor and include")
    with tempfile.TemporaryDirectory(prefix="python-buffer-order-", dir="runtime") as directory:
        root = Path(directory).resolve()
        package = root / "cetta"
        package.mkdir()
        for path in Path("python/cetta").glob("*.py"):
            shutil.copy2(path, package / path.name)
        (package / "resources").symlink_to(Path.cwd(), target_is_directory=True)
        output = package / ("_native" + sysconfig.get_config_var("EXT_SUFFIX"))
        environment = os.environ.copy()
        environment["PYTHONPATH"] = str(root)
        for mutated in (False, True):
            body = buffers.replace(before, "size_t prefix = 0u;") if mutated else buffers
            native = root / "native.c"
            native.write_text(source.replace(include, body))
            subprocess.run(compiler + flags + ["-shared", str(native), args.library,
                "-o", str(output), *link], check=True)
            result = subprocess.run([sys.executable, "-c", probe], cwd=root,
                env=environment, capture_output=True, text=True, timeout=30)
            if not mutated:
                if result.returncode:
                    raise RuntimeError("baseline export failed\n" + result.stderr)
                print("PASS: Python foreign-order buffer export and immutable sharing", flush=True)
            elif result.returncode == 0 or "AssertionError: foreign byte order disappeared" not in result.stderr:
                raise RuntimeError("format mutation missed its intended assertion\n" + result.stderr)
            else:
                print("PASS: dropped Python byte-order prefix rejected", flush=True)


if __name__ == "__main__":
    main()
