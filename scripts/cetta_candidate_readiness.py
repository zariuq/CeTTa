"""Bounded PeTTa/HE/Python checks, separate from release qualification."""

from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import sysconfig
import time

from cetta_readiness_model import terminate_process_session


SCHEMA = "cetta.candidate-readiness.v1"
INPUT_DIRS = {"src", "include", "native", "lib", "langdef", "python", "tests", "mk",
              "scripts", "tools", "rust", "experiments", "benchmarks", "examples",
              "specs", "vendor", "showcase", "repos"}
INPUT_FILES = {"Makefile", "setup.py", "setup.cfg", "pyproject.toml", "MANIFEST.in",
               "VERSION", "Cargo.toml", "Cargo.lock"}


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def file_digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def source_identity(root):
    """Use working bytes, so committing or editing prose does not rerun C tests."""
    names = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=root).decode().split("\0")
    inputs = {}
    for name in sorted(set(names) - {""}):
        path = Path(name)
        if (path.parts[0] in INPUT_DIRS or name in INPUT_FILES):
            if path.suffix.lower() not in {".md", ".tex", ".pdf", ".ipynb"}:
                absolute = root / path
                inputs[name] = file_digest(absolute) if absolute.is_file() else None
    return digest(inputs)


def environment_identity(environment):
    # Store only the digest: environments can contain credentials. Include test
    # switches and Make overrides rather than reusing results across them.
    transient = {"PWD", "OLDPWD", "SHLVL", "_", "MAKELEVEL"}
    return digest({k: v for k, v in environment.items() if k not in transient})


def commands(build, jobs, targets):
    make = ["make", "--no-print-directory", f"BUILD={build}"]
    steps = [("build", [*make, f"-j{jobs}", "all"], False)]
    if targets:
        steps.extend((target, [*make, "-j1", target], True) for target in targets)
        return steps
    steps.extend([
        ("ordinary", [*make, "-j1", "test"], True),
        ("petta", [*make, "-j1", "test-petta-corrected-stable"], True),
        ("he", [*make, "-j1", "test-he-evaluated-values",
                "test-he-guarded-equation-plan"], True),
        ("lifetime", [*make, "-j1", "test-worker-resource-publication",
                      "test-fallback-eval-session", "test-cli-diagnostics"], True),
        ("python-build", [sys.executable, "setup.py", "build_ext", "--inplace"], False),
        ("python", [sys.executable, "-m", "unittest", "discover",
                    "-s", "python/tests"], True),
    ])
    return steps


def artifacts(root, step):
    if step == "python":
        names = [*sorted((root / "python/cetta").glob("_native*.so")),
                 root / "python/cetta/resources/runtime/cetta-proc-scope"]
    else:
        names = [root / "cetta", root / "cetta.proc-scope"]
    return {str(path.relative_to(root)): file_digest(path)
            for path in names if path.is_file()}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def cached_pass(path, key):
    try:
        result = json.loads(path.read_text())
        log = Path(result["log"])
        if (result["key"] == key and result["status"] == "passed"
                and file_digest(log) == result["log_sha256"]):
            return {**result, "reused": True}
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return None


def run_step(command, root, log, environment, deadline):
    """The remaining campaign budget also bounds silent or blocked children."""
    started = time.monotonic()
    if started >= deadline:
        return {"status": "incomplete", "reason": "time budget exhausted"}
    log.parent.mkdir(parents=True, exist_ok=True)
    status = "failed"
    code = None
    with log.open("wb") as output:
        process = subprocess.Popen(command, cwd=root, env=environment,
                                   stdout=output, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            code = process.wait(timeout=max(0.001, deadline - time.monotonic()))
            status = "passed" if code == 0 else "failed"
        except subprocess.TimeoutExpired:
            status = "incomplete"
        except KeyboardInterrupt:
            status = "interrupted"
        finally:
            terminate_process_session(process)
            process.wait()
    return {"status": status, "exit_status": code,
            "elapsed_seconds": round(time.monotonic() - started, 3),
            "log": str(log.resolve()), "log_sha256": file_digest(log)}


def run_candidate(args, root):
    if (not math.isfinite(args.minutes) or args.minutes <= 0 or args.jobs < 1
            or any(not re.fullmatch(r"test-[A-Za-z0-9_-]+", t) for t in args.target)):
        print("FAIL: use a positive budget/job count and test-* target names", file=sys.stderr)
        return 2
    if args.reuse_common_from or args.reuse_common_summary_sha256:
        print("FAIL: common-prefix composition applies to the legacy tiers", file=sys.stderr)
        return 2
    scope = "focused" if args.target else "candidate"
    steps = commands(args.build, args.jobs, args.target)
    print(f"{scope}: {args.minutes:g}-minute total budget (including builds).")
    print("Scope: ordinary development checks; exhaustive release qualification is separate.")
    if args.dry_run:
        for name, command, reusable in steps:
            print(f"  {name}: {shlex.join(command)}" + (" [reuse passing evidence]" if reusable else ""))
        return 0

    started = time.monotonic()
    deadline = started + args.minutes * 60
    base = root / "runtime/main-readiness/candidate"
    base.mkdir(parents=True, exist_ok=True)
    # One runner at a time: builds in the same checkout share their public CLI.
    import fcntl
    with (base / "runner.lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("FAIL: a candidate check is already running in this checkout", file=sys.stderr)
            return 2
        evidence = base / str(time.time_ns())
        evidence.mkdir()
        environment = dict(os.environ)
        environment["PYTHONPATH"] = str(root / "python")
        compiler = shlex.split(environment.get("CC", "gcc"))
        try:
            toolchain = subprocess.check_output(
                [*compiler, "--version"], stderr=subprocess.STDOUT,
                timeout=min(5, args.minutes * 60)).decode(errors="replace")
        except (OSError, subprocess.SubprocessError) as error:
            print(f"FAIL: cannot identify compiler: {error}", file=sys.stderr)
            return 2
        common = {"schema": SCHEMA, "build": args.build, "python": sys.version,
                  "platform": sysconfig.get_platform(), "compiler": toolchain,
                  "environment": environment_identity(environment)}
        summary = {**common, "scope": scope, "budget_minutes": args.minutes,
                   "status": "incomplete", "steps": []}
        frozen_source = None
        for name, command, reusable in steps:
            if time.monotonic() >= deadline:
                summary["reason"] = "time budget exhausted"
                break
            before = source_identity(root)
            if frozen_source is not None and before != frozen_source:
                summary["reason"] = "source changed during the check; rerun on the new source"
                break
            if reusable and frozen_source is None:
                frozen_source = before
            key = digest({**common, "source": before, "command": command,
                          "artifacts": artifacts(root, name)})
            cache = base / "cache" / f"{key}.json"
            result = cached_pass(cache, key) if reusable and not args.fresh else None
            if result:
                print(f"REUSE {name}", flush=True)
            else:
                print(f"RUN {name} (log: {evidence.relative_to(root)}/{name}.log)", flush=True)
                try:
                    result = run_step(command, root, evidence / f"{name}.log",
                                      environment, deadline)
                except OSError as error:
                    result = {"status": "failed", "reason": str(error)}
                result.update(key=key, command=command, reused=False)
                if result["status"] == "passed" and source_identity(root) != before:
                    if reusable:
                        result.update(status="incomplete", reason="source changed during the check")
                if reusable and result["status"] == "passed":
                    write_json(cache, result)
                print(f"{result['status'].upper()} {name}", flush=True)
            summary["steps"].append({"name": name, **result})
            summary["elapsed_seconds"] = round(time.monotonic() - started, 3)
            write_json(evidence / "summary.json", summary)
            if result["status"] != "passed":
                summary["status"] = result["status"]
                break
        else:
            if time.monotonic() >= deadline:
                summary["reason"] = "time budget exhausted"
            elif source_identity(root) == frozen_source:
                summary["status"] = "passed"
            else:
                summary["reason"] = "source changed during the check"
        summary["source"] = frozen_source
        summary["elapsed_seconds"] = round(time.monotonic() - started, 3)
        write_json(evidence / "summary.json", summary)
        write_json(base / "latest.json", summary)
        print(f"{scope.upper()} {summary['status'].upper()}: {evidence.relative_to(root)}/summary.json")
        print("Passing checks are saved individually; rerun the same command after a repair.")
        return (0 if summary["status"] == "passed" else
                130 if summary["status"] == "interrupted" else
                2 if summary["status"] == "incomplete" else 1)
