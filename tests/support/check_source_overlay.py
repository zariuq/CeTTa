#!/usr/bin/env python3
"""Run the ordinary Space overlay contract against a private durable source."""
import pathlib
import subprocess
import sys
import tempfile


if __name__ == "__main__":
    binary = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="space-source-overlay-") as root:
        store = pathlib.Path(root) / "store"
        store.mkdir(mode=0o700)
        subprocess.run([str(binary), str(store)], check=True)
