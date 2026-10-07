#!/usr/bin/env python3
"""Exercise atomic C object publication across concurrent recursive makes."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/atomic-object-publication")
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()
    work = args.output.resolve() / uuid.uuid4().hex
    work.mkdir(parents=True)
    (work / "payload.h").write_text("#define RETURN_VALUE 7\n")
    (work / "payload.c").write_text('#include "payload.h"\nint published_value(void) { return RETURN_VALUE; }\n')
    (work / "main.c").write_text('#include <stdio.h>\nint published_value(void);\nint main(void) { printf("%d\\n", published_value()); }\n')
    fixture = work / "fixture.mk"
    fixture.write_text("""$(ATOMIC_GATE_ROOT)/published.o: $(ATOMIC_GATE_ROOT)/payload.c $(ATOMIC_GATE_ROOT)/payload.h FORCE
\t$(call compile_c_object,$(CFLAGS) $(DEPFLAGS))
$(ATOMIC_GATE_ROOT)/unsafe.o: $(ATOMIC_GATE_ROOT)/payload.c $(ATOMIC_GATE_ROOT)/payload.h FORCE
\t$(CC) $(CFLAGS) $(DEPFLAGS) -MT "$@" -MF "$(@:.o=.d)" -c -o "$@" "$<"
""")
    driver = work / "compiler.py"
    driver.write_text("""import os, pathlib, subprocess, sys, time
args = sys.argv[1:]
pathlib.Path(args[args.index('-o') + 1]).write_bytes(b'')
marker = os.environ.get('ATOMIC_GATE_MARKER')
if marker:
    pathlib.Path(marker).write_text('output opened')
    deadline = time.monotonic() + float(os.environ['ATOMIC_GATE_TIMEOUT'])
    while not pathlib.Path(os.environ['ATOMIC_GATE_RELEASE']).exists():
        if time.monotonic() >= deadline:
            raise SystemExit(98)
        time.sleep(0.01)
if os.environ.get('ATOMIC_GATE_FAIL') == '1':
    raise SystemExit(42)
raise SystemExit(subprocess.call(['cc', *args]))
""")
    command = ["make", "-s", "--no-print-directory", "-f", "Makefile", "-f", str(fixture),
               "ATOMIC_GATE_ROOT=" + str(work), "CFLAGS=-O0 -Wall -Werror -std=c11",
               "CC=" + shlex.quote(sys.executable) + " " + shlex.quote(str(driver))]
    children: list[tuple[subprocess.Popen, object]] = []
    checks = 0

    def check(ok: bool, label: str) -> None:
        nonlocal checks
        if not ok:
            raise AssertionError(label)
        checks += 1
        print("PASS: atomic object " + label, flush=True)

    def launch(label: str, target: str = "published", fail: bool = False):
        env = os.environ.copy()
        # These are independent competing makes, not children sharing a jobserver.
        for key in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL"):
            env.pop(key, None)
        env.update(ATOMIC_GATE_MARKER=str(work / (label + ".started")),
                   ATOMIC_GATE_RELEASE=str(work / (label + ".release")),
                   ATOMIC_GATE_TIMEOUT=str(args.timeout),
                   ATOMIC_GATE_FAIL="1" if fail else "0")
        log = (work / (label + ".log")).open("w")
        proc = subprocess.Popen(command + [str(work / (target + ".o"))], cwd=ROOT,
                                env=env, stdout=log, stderr=subprocess.STDOUT)
        children.append((proc, log))
        return proc

    def await_open(label: str, proc: subprocess.Popen) -> None:
        deadline = time.monotonic() + args.timeout
        while not (work / (label + ".started")).exists():
            if proc.poll() is not None or time.monotonic() >= deadline:
                raise AssertionError(label + " compiler did not open its output")
            time.sleep(0.01)

    def release(label: str, proc: subprocess.Popen) -> int:
        (work / (label + ".release")).write_text("continue")
        return proc.wait(timeout=args.timeout)

    def linked_value(name: str = "published") -> str:
        exe = work / (name + "-probe")
        subprocess.run(["cc", str(work / "main.c"), str(work / (name + ".o")), "-o", str(exe)],
                       check=True, capture_output=True, timeout=args.timeout)
        return subprocess.check_output([str(exe)], text=True, timeout=args.timeout).strip()

    try:
        seed = launch("seed")
        await_open("seed", seed)
        check(release("seed", seed) == 0 and linked_value() == "7", "initial object links")
        obj = work / "published.o"
        dep = work / "published.d"
        before = digest(obj), digest(dep)
        (work / "payload.h").write_text("#define RETURN_VALUE 11\n")
        first, second = launch("first"), launch("second")
        await_open("first", first)
        await_open("second", second)
        check((digest(obj), digest(dep)) == before and linked_value() == "7",
              "two open compiler outputs leave the published pair intact")
        check(release("first", first) == 0 and linked_value() == "11",
              "first completed compiler publishes a linkable object")
        check(release("second", second) == 0 and linked_value() == "11",
              "second completed compiler publishes a linkable object")
        check(dep.read_text().startswith(str(obj) + ":") and str(work / "payload.h") in dep.read_text(),
              "dependencies name the final target and its header")
        before = digest(obj), digest(dep)
        failed = launch("failed", fail=True)
        await_open("failed", failed)
        check((digest(obj), digest(dep)) == before and linked_value() == "11",
              "failing compiler cannot truncate the published object")
        check(release("failed", failed) != 0 and (digest(obj), digest(dep)) == before,
              "compiler failure preserves both previous outputs")
        (work / "unsafe.o").write_bytes(obj.read_bytes())
        (work / "unsafe.d").write_bytes(dep.read_bytes())
        unsafe = launch("unsafe", target="unsafe", fail=True)
        await_open("unsafe", unsafe)
        check((work / "unsafe.o").stat().st_size == 0,
              "in-place negative control exposes the interrupted output")
        check(release("unsafe", unsafe) != 0,
              "in-place negative control fails without publishing a replacement")
        print(f"Atomic object publication: {checks} passed, 0 failed")
        return 0
    finally:
        for proc, log in children:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=args.timeout)
            log.close()


if __name__ == "__main__":
    raise SystemExit(main())
