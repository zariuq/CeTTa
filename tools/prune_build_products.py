#!/usr/bin/env python3
"""Remove build products of configurations that are no longer built.

Usage: prune_build_products.py [--keep-days N] [--delete] [--root DIR]

Every build configuration names its products by its object tag
(BUILD_OBJ_TAG): objects beside their sources, `<src>.<tag>[...].{o,d}`;
binaries `runtime/cetta[-stage0|-parser-compiler]-<tag>` and
`runtime/<test>-<tag>[-runtime-stats]`; configuration headers and stamps
`runtime/bootstrap/build_config[.stage0].<tag>.{h,stamp}`; baked parser
files `runtime/bootstrap/parser_baked_v1.<tag>.<key>.*`; generated
directories `runtime/generated/*<tag>*`; and mutation suites
`runtime/bootstrap/<suite>-mutations-<tag>/<source-hash>/`.

A tag is a name with a configuration file in runtime/bootstrap.  A tag is
kept when it is the default build's or when any of its products changed in
the last N days (7 by default).  Of every other tag, the files that carry it
in one of the forms above are removed.  Within a kept tag's mutation suites,
the newest source hash and every one changed in the last N days are kept.

Only files git reports as ignored and untracked are candidates, so tracked
files and work files are never touched; a file that carries no tag in a
known form (a binary copied under a name of its own, a work directory) is
left in place and listed.  Without --delete nothing is removed: the
products the run would remove are summarized.
"""
import argparse
import os
import re
import subprocess
import sys
import time

CONFIG_RE = re.compile(r'^runtime/bootstrap/build_config(?:\.stage0)?\.(.+)\.(?:h|stamp)$')
MUTATIONS_RE = re.compile(r'^runtime/bootstrap/([^/]+)-mutations-([^/]+)/([^/]+)(?:/|$)')


def ignored_files(root):
    out = subprocess.run(
        ["git", "-C", root, "ls-files", "--others", "--ignored",
         "--exclude-standard", "-z"],
        check=True, capture_output=True).stdout
    return [p for p in out.decode("utf-8", "surrogateescape").split("\0") if p]


def default_tag(root):
    out = subprocess.run(
        ["make", "-s", "-C", root, "--no-print-directory",
         "--eval", "prune-print-tag: ; @echo $(BUILD_OBJ_TAG)", "prune-print-tag"],
        capture_output=True, text=True, errors="replace").stdout.strip().splitlines()
    return out[-1].strip() if out else None


def tag_of(path, tags):
    """The tag a build product carries, or None."""
    m = CONFIG_RE.match(path)
    if m:
        return m.group(1)
    m = MUTATIONS_RE.match(path)
    if m and m.group(2) in tags:
        return m.group(2)
    base = os.path.basename(path)
    if path.startswith("runtime/"):
        # binaries and parser files: the longest known tag in the name
        best = None
        for t in tags:
            if (base.endswith("-" + t) or base.endswith("-" + t + "-runtime-stats")
                    or ("." + t + ".") in base or ("-" + t + "/") in path
                    or ("-" + t + "-") in base or ("/" + t + "/") in path
                    or ("-" + t + "/") in path or path.endswith("-" + t)):
                if best is None or len(t) > len(best):
                    best = t
        if best:
            return best
        for t in tags:
            if ("/" + "prepared-" + t + "/") in path or ("parser-artifacts-" + t + "/") in path:
                return t
        return None
    # objects and dependency files beside their sources
    if base.endswith((".o", ".d")):
        stem = base[:-2]
        for suffix in (".runtime-stats", ".stage0"):
            if stem.endswith(suffix):
                stem = stem[:-len(suffix)]
        for suffix in (".runtime-stats", ".stage0"):
            if stem.endswith(suffix):
                stem = stem[:-len(suffix)]
        best = None
        for t in tags:
            if stem.endswith("." + t) and (best is None or len(t) > len(best)):
                best = t
        return best
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--keep-days", type=float, default=7.0)
    ap.add_argument("--delete", action="store_true")
    ap.add_argument("--show", action="store_true",
                    help="list what would be removed, grouped by directory")
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    cutoff = time.time() - args.keep_days * 86400.0
    files = ignored_files(root)
    tags = {m.group(1) for p in files for m in [CONFIG_RE.match(p)] if m}
    current = default_tag(root)
    newest, products, untagged = {}, {}, []
    sizes = {}
    for p in files:
        full = os.path.join(root, p)
        try:
            st = os.lstat(full)
        except FileNotFoundError:
            continue
        sizes[p] = st.st_size
        t = tag_of(p, tags)
        if t is None:
            untagged.append(p)
            continue
        products.setdefault(t, []).append(p)
        newest[t] = max(newest.get(t, 0.0), st.st_mtime)
    kept_tags = {t for t in products if t == current or newest.get(t, 0.0) >= cutoff}
    remove = []
    for t, paths in products.items():
        if t not in kept_tags:
            remove.extend(paths)
            continue
        # a kept tag's mutation suites: the newest source hash and recent ones stay
        suites = {}
        for p in paths:
            m = MUTATIONS_RE.match(p)
            if m:
                suites.setdefault((m.group(1), m.group(2)), {}).setdefault(m.group(3), []).append(p)
        for hashes in suites.values():
            stamp = {h: max(os.lstat(os.path.join(root, q)).st_mtime for q in ps)
                     for h, ps in hashes.items()}
            latest = max(stamp, key=stamp.get)
            for h, ps in hashes.items():
                if h != latest and stamp[h] < cutoff:
                    remove.extend(ps)
    total = sum(sizes.get(p, 0) for p in remove)
    print(f"default tag: {current}")
    print(f"tags: {len(tags)}, kept {len(kept_tags)}, stale {len(set(products) - kept_tags)}")
    print(f"{'removing' if args.delete else 'would remove'} {len(remove)} files, "
          f"{total / 2**30:.2f} GiB")
    print(f"untagged ignored files left in place: {len(untagged)}, "
          f"{sum(sizes.get(p, 0) for p in untagged) / 2**30:.2f} GiB")
    if args.show:
        groups = {}
        for p in remove:
            parts = p.split("/")
            key = "/".join(parts[:3]) if parts[0] == "runtime" else parts[0] + "/*." + "o|d"
            n, b = groups.get(key, (0, 0))
            groups[key] = (n + 1, b + sizes.get(p, 0))
        for key, (n, b) in sorted(groups.items(), key=lambda kv: -kv[1][1])[:40]:
            print(f"  {b / 2**30:8.2f} GiB {n:7d} files  {key}")
    if not args.delete:
        return 0
    removed_dirs = set()
    for p in remove:
        full = os.path.join(root, p)
        try:
            os.unlink(full)
        except FileNotFoundError:
            pass
        removed_dirs.add(os.path.dirname(full))
    # directories the removal emptied, innermost first, never above runtime/ or the tree
    for d in sorted(removed_dirs, key=len, reverse=True):
        while d.startswith(root + os.sep) and d != root:
            try:
                os.rmdir(d)
            except OSError:
                break
            d = os.path.dirname(d)
    return 0


if __name__ == "__main__":
    sys.exit(main())
