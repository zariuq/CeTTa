#!/usr/bin/env python3
"""tools/prune_build_products.py removes exactly the products of stale build
configurations, in a throwaway git tree.

The tree holds a stale tag (configuration and products ten days old), a
recent tag, a binary under a name of its own with no configuration, an
ignored work file, a tracked file that looks like a product, and a recent
tag's mutation suite with an old, a newest and a recent source hash.  A dry
run removes nothing; --delete removes the stale tag's products and the old
source hash, and leaves everything else.
"""
import os
import subprocess
import sys
import tempfile
import time

TOOL = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), "tools", "prune_build_products.py")
DAY = 86400.0

STALE = "core.matrix-0000aaaa-0000bbbbcccc"
RECENT = "python.json-gslt"


def touch(root, rel, age_days):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(rel)
    t = time.time() - age_days * DAY
    os.utime(path, (t, t))


def main():
    with tempfile.TemporaryDirectory(prefix="cetta-prune-check-") as root:
        subprocess.run(["git", "init", "-q", root], check=True)
        with open(os.path.join(root, ".gitignore"), "w") as f:
            f.write("*.o\n*.d\nruntime/\nwork/\n")
        with open(os.path.join(root, "Makefile"), "w") as f:
            f.write(f"BUILD_OBJ_TAG = {RECENT}\n")
        touch(root, "src/tracked.c", 0)
        touch(root, f"src/pinned.{STALE}.o", 10)   # tracked although it looks like a product
        subprocess.run(["git", "-C", root, "add", "-f", ".gitignore", "Makefile",
                        "src/tracked.c", f"src/pinned.{STALE}.o"], check=True)
        stale = [f"runtime/bootstrap/build_config.{STALE}.h",
                 f"runtime/bootstrap/build_config.{STALE}.stamp",
                 f"src/tracked.{STALE}.o", f"src/tracked.{STALE}.stage0.d",
                 f"runtime/cetta-{STALE}", f"runtime/test_space-{STALE}-runtime-stats",
                 f"runtime/bootstrap/parser_baked_v1.{STALE}.k1.o"]
        recent = [f"runtime/bootstrap/build_config.{RECENT}.h",
                  f"src/tracked.{RECENT}.o", f"runtime/cetta-{RECENT}"]
        mutations = f"runtime/bootstrap/recursive-producer-mutations-{RECENT}"
        for rel in stale:
            touch(root, rel, 10)
        for rel in recent:
            touch(root, rel, 0)
        touch(root, f"{mutations}/aaaa/probe", 20)    # old: removed
        touch(root, f"{mutations}/bbbb/probe", 9)     # newest of the old ones: still old, removed
        touch(root, f"{mutations}/cccc/probe", 1)     # recent: kept (and newest)
        kept_untagged = ["runtime/cetta-before-some-change", "work/notes.txt"]
        for rel in kept_untagged:
            touch(root, rel, 30)

        def exists(rel):
            return os.path.exists(os.path.join(root, rel))

        dry = subprocess.run([sys.executable, TOOL, "--root", root],
                             capture_output=True, text=True)
        if dry.returncode != 0 or not all(exists(r) for r in stale):
            print("FAIL: a dry run changed the tree or failed\n" + dry.stdout + dry.stderr)
            return 1
        run = subprocess.run([sys.executable, TOOL, "--root", root, "--delete"],
                             capture_output=True, text=True)
        if run.returncode != 0:
            print("FAIL: --delete failed\n" + run.stdout + run.stderr)
            return 1
        problems = []
        problems += [f"stale product kept: {r}" for r in stale if exists(r)]
        problems += [f"recent product removed: {r}" for r in recent if not exists(r)]
        problems += [f"untagged file removed: {r}" for r in kept_untagged if not exists(r)]
        if not exists(f"src/pinned.{STALE}.o"):
            problems.append("tracked file removed")
        if exists(f"{mutations}/aaaa/probe") or exists(f"{mutations}/bbbb/probe"):
            problems.append("an old mutation source hash was kept")
        if not exists(f"{mutations}/cccc/probe"):
            problems.append("the recent mutation source hash was removed")
        if problems:
            print("FAIL: " + "; ".join(problems))
            return 1
    print("PASS: build products of stale configurations and old mutation sources go; "
          "the current and recent configurations, untagged files and tracked files stay")
    return 0


if __name__ == "__main__":
    sys.exit(main())
