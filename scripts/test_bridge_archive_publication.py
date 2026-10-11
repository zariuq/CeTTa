#!/usr/bin/env python3
"""Check archive publication and preservation of the old file on copy failure."""

import os
from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    work = Path(tempfile.mkdtemp(prefix="bridge-publication-", dir=root / "runtime"))
    source = work / "source.a"
    published = work / "published.a"
    stamp = work / "input.stamp"
    workspace = work / "Cargo.toml"
    stamp.write_text("publication input\n")
    workspace.write_text("publication input\n")
    first = b"original archive\n" * 1024
    second = b"replacement archive\n" * 2048
    source.write_bytes(first)
    command = [
        "make", "--no-print-directory", "-s", "BUILD=core", "CETTA_TEST_ISOLATED=1",
        f"--old-file={stamp}", f"--old-file={workspace}",
        f"MORK_BRIDGE_BUILD_STAMP={stamp}",
        f"MORK_BRIDGE_WORKSPACE_MANIFEST={workspace}",
        f"MORK_BRIDGE_FEATURE_STATICLIB={published}",
        f"MORK_BRIDGE_STATICLIB={source}",
        f"MORK_BRIDGE_BUILD_LOCK={work / 'publish.lock'}",
        str(published),
    ]
    subprocess.run(command, cwd=root, check=True)
    assert published.read_bytes() == first

    fault_dir = work / "fault-bin"
    fault_dir.mkdir()
    copier = fault_dir / "cp"
    copier.write_text(
        '#!/bin/sh\n'
        'head -c 8 "$2" > "$3"\n'
        'exit 23\n'
    )
    copier.chmod(0o755)
    source.write_bytes(second)
    environment = dict(os.environ, PATH=str(fault_dir) + os.pathsep + os.environ["PATH"])
    failed = subprocess.run(
        command[:-1] + [f"--assume-new={stamp}", str(published)],
        cwd=root, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, check=False,
    )
    assert failed.returncode != 0, "the injected partial copy must fail"
    assert published.read_bytes() == first, "failed publication must preserve the old archive"

    with published.open("rb") as old_reader:
        subprocess.run(command[:-1] + [f"--assume-new={stamp}", str(published)],
                       cwd=root, check=True)
        assert old_reader.read() == first, "an existing reader must retain its old complete inode"
    assert published.read_bytes() == second
    print("PASS: archive publication is atomic for failed copies and existing readers")


if __name__ == "__main__":
    main()
