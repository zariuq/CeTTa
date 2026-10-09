#!/usr/bin/env python3
"""Compare finite module inventories, nested table growth and fetched module
roots with HE references."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from metta_result_observation import output_observation


def identity(path: Path) -> dict:
    return {"sha256": sha256(path.read_bytes()).hexdigest()}


# A reference release may deadlock on these cases: an `include` reached from
# inside another `include` locks one mutex twice in HE 0.2.10. Such a reference
# gives no observation for the case; another reference must.
REFERENCE_MAY_DEADLOCK = {"nested-transactions"}

# A reference release may lack a behaviour: HE 0.2.10 fetches a git module and
# runs its code, but cannot then import it by name. A reference that answers
# with this text gives no observation for the case; another reference must.
REFERENCE_MAY_LACK = {"git-module": "Failed to resolve module top:gitmod_demo"}


def run(command: list[str], directory: Path, deadlock_wait: float | None = None) -> dict | None:
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONUNBUFFERED="1",
                       # Fetched modules are cached beside the case, not in the home directory.
                       XDG_CACHE_HOME=str(directory / "cache"))
    environment.pop("PYTHONPATH", None)
    process = subprocess.Popen(command, cwd=directory, env=environment,
                               text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=deadlock_wait or 120)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate()
        if deadlock_wait:
            return None
        raise RuntimeError("Diagnostic timeout; no language result was inferred")
    return {"stdout": stdout, "stderr": stderr, "exit_status": process.returncode}


def reference_worker(name: str, directory: Path) -> None:
    import hyperon
    import hyperonpy
    from hyperon import Environment, MeTTa

    if name == "identity":
        print(json.dumps({"python": identity(Path(sys.executable)),
                          "package": identity(Path(hyperon.__file__)),
                          "extension": identity(Path(hyperonpy.__file__))}))
        return
    environment = {"working_dir": str(directory)}
    if name == "git-module":
        # The reference caches a fetched module below its configuration directory.
        own = sha256(sys.executable.encode()).hexdigest()[:12]
        environment.update(config_dir=str(directory / f"reference-config-{own}"),
                           create_config=True)
    runner = MeTTa(env_builder=Environment.custom_env(**environment))
    for answers in runner.run((directory / "main.metta").read_text()):
        print("[" + ", ".join(map(str, answers)) + "]", flush=True)


def make_case(directory: Path, name: str) -> str:
    directory.mkdir()
    lines = []
    if name == "loaded-inventory":
        count = 70
        for index in range(count):
            (directory / f"item{index}.metta").write_text(f"(inventory-row {index})\n")
            lines.append(f"!(import! &self item{index})")
        lines.append("!(let $rows (collapse (match &self (inventory-row $x) $x)) (size-atom $rows))")
        expected = "[()]\n" * count + f"[{count}]\n"
    elif name == "inline-inventory":
        count = 320
        for index in range(count):
            (directory / f"item{index}.metta").write_text(f"(inventory-row {index})\n! ()\n")
            lines.append(f"!(include item{index})")
        lines.append("!(let $rows (collapse (match &self (inventory-row $x) $x)) (size-atom $rows))")
        expected = "[()]\n" * count + f"[{count}]\n"
    elif name == "nested-growth":
        count = 40
        for index in range(count):
            body = f"(nested-row {index})\n"
            if index + 1 < count:
                body += f"!(import! &self item{index + 1})\n"
            # Access the still-active parent after child initialization has
            # grown the inventory: record addresses must remain valid.
            body += f"!(bind! own-{index} &self)\n"
            (directory / f"item{index}.metta").write_text(body)
        lines = ["!(import! &self item0)",
                 "!(let $rows (collapse (match &self (nested-row $x) $x)) (size-atom $rows))",
                 "!(== (mod-space! top:item0) (mod-space! top:item0))"]
        expected = f"[()]\n[{count}]\n[True]\n"
    elif name == "registered-roots":
        count = 40
        for index in range(count):
            root = directory / f"root{index}"
            root.mkdir()
            (root / "module.metta").write_text(f"(root-row {index})\n")
            lines.append(f"!(register-module! root{index})")
        for index in range(count):
            lines.append(f"!(import! &self root{index})")
        lines.append("!(let $rows (collapse (match &self (root-row $x) $x)) (size-atom $rows))")
        expected = "[()]\n" * (2 * count) + f"[{count}]\n"
    elif name == "nested-transactions":
        count = 40
        for index in range(count):
            # Each module includes a file into its own space, and that file
            # imports the next module: one open transaction per module.
            (directory / f"owner{index}.metta").write_text(
                f"(owner-row {index})\n!(include part{index})\n")
            body = f"(part-row {index})\n"
            if index + 1 < count:
                body += f"!(import! &self owner{index + 1})\n"
            (directory / f"part{index}.metta").write_text(body)
        lines = ["!(import! &self owner0)",
                 "!(let $rows (collapse (match &self (owner-row $x) $x)) (size-atom $rows))",
                 "!(let $rows (collapse (match &self (part-row $x) $x)) (size-atom $rows))"]
        expected = f"[()]\n[{count}]\n[{count}]\n"
    elif name == "long-names":
        file_name = "m" + "x" * 149
        root_name = "d" + "y" * 149
        (directory / f"{file_name}.metta").write_text("(long-file-row 1)\n")
        (directory / root_name).mkdir()
        (directory / root_name / "module.metta").write_text("(long-root-row 1)\n")
        lines = [f"!(import! &self {file_name})",
                 "!(match &self (long-file-row $x) $x)",
                 f"!(register-module! {root_name})",
                 f"!(import! &self {root_name})",
                 "!(match &self (long-root-row $x) $x)"]
        expected = "[()]\n[1]\n[()]\n[()]\n[1]\n"
    elif name == "git-module":
        origin = directory / "origin" / "gitmod_demo"
        origin.mkdir(parents=True)
        (origin / "module.metta").write_text("(git-row 1)\n!(println! git-module-ran)\n")
        (origin / "part.metta").write_text("(git-part 2)\n")
        git = ["git", "-C", str(origin), "-c", "user.name=fixture",
               "-c", "user.email=fixture@example.invalid"]
        for arguments in (["init", "-q"], ["add", "-A"], ["commit", "-q", "-m", "fixture"]):
            subprocess.run(git + arguments, check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
        attempt = (f'!(case (git-module! "file://{origin}") '
                   "(((Error $a $b) refused) ($ok registered)))")
        # The module's code runs when it is fetched, before the next query.
        lines = [attempt, "!(println! after-git-module)",
                 "!(import! &self gitmod_demo)", "!(match &self (git-row $x) $x)",
                 "!(import! &self gitmod_demo:part)", "!(match &self (git-part $x) $x)",
                 attempt]
        expected = ("git-module-ran\nafter-git-module\n[registered]\n[()]\n[()]\n[1]\n"
                    "[()]\n[2]\n[refused]\n")
    else:
        assert name == "nested-resources"
        count = 40
        parent = directory
        for index in range(count):
            owner = parent / f"node{index}"
            owner.mkdir()
            body = f"(nested-row {index})\n"
            if index + 1 < count:
                body += f"!(import! &self node{index + 1})\n"
            # A child must restore the parent's resource directory even
            # after the retained scope stack has grown. Distinct names
            # prevent a previously loaded resource from masking the lookup.
            body += f"!(import! &self local{index})\n"
            (owner / "module.metta").write_text(body)
            (owner / f"local{index}.metta").write_text(
                f"(local-row {index})\n! ()\n")
            parent = owner
        lines = ["!(import! &self node0)",
                 "!(let $rows (collapse (match &self (nested-row $x) $x)) (size-atom $rows))",
                 "!(let $rows (collapse (match &self (local-row $x) $x)) (size-atom $rows))",
                 "!(== (mod-space! top:node0) (mod-space! top:node0))"]
        expected = f"[()]\n[{count}]\n[{count}]\n[True]\n"
    (directory / "main.metta").write_text("\n".join(lines) + "\n")
    return expected


def profile_expectation(name: str, expected: str, profile: str) -> str:
    if name == "git-module" and profile != "he-compat":
        # The default profiles keep their namespace-upsert registration.
        lines = expected.splitlines()
        assert lines[-1] == "[refused]", lines
        lines[-1] = "[registered]"
        return "\n".join(lines) + "\n"
    return expected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--reference-python", required=True, action="append", type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--receipt", required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="module-capacity-", dir=args.work_dir.resolve()))
    cases = {}
    for name in ("loaded-inventory", "inline-inventory", "nested-growth", "nested-resources",
                 "registered-roots", "nested-transactions", "long-names", "git-module"):
        directory = work / name
        cases[name] = (directory, make_case(directory, name))
    references = []
    records = []
    deadlocks = []
    lacks = []
    observed_cases = set()
    binary_identity = identity(binary)
    for python in args.reference_python:
        command = [str(python.absolute()), "-I", str(Path(__file__).resolve()), "--reference"]
        observed = run(command + ["identity", str(work)], work)
        assert observed["exit_status"] == 0 and not observed["stderr"], observed
        reference_identity = json.loads(observed["stdout"])
        references.append(reference_identity)
        for name, (directory, expected) in cases.items():
            reference = run(command + [name, str(directory)], directory,
                            deadlock_wait=30 if name in REFERENCE_MAY_DEADLOCK else None)
            if reference is None:
                deadlocks.append({"case": name, "reference_identity": reference_identity})
                print(f"NO-REFERENCE {name}: this reference deadlocks", flush=True)
                continue
            if name in REFERENCE_MAY_LACK and REFERENCE_MAY_LACK[name] in reference["stdout"]:
                lacks.append({"case": name, "reference_identity": reference_identity,
                              "reference": reference})
                print(f"NO-REFERENCE {name}: this reference lacks the behaviour", flush=True)
                continue
            observed_cases.add(name)
            assert reference["exit_status"] == 0 and not reference["stderr"], reference
            assert output_observation(reference["stdout"]) == output_observation(expected), reference
            for profile in ("he", "he-compat", "extended"):
                actual = run([str(binary), "--lang", "he", "--profile", profile,
                              str(directory / "main.metta")], directory)
                wanted = profile_expectation(name, expected, profile)
                agrees = actual["exit_status"] == 0 and not actual["stderr"] and (
                    output_observation(actual["stdout"]) == output_observation(wanted))
                records.append({"case": name, "profile": profile,
                                "reference_identity": reference_identity,
                                "reference": reference, "expected": wanted,
                                "actual": actual, "agrees": agrees})
                print(f"{'PASS' if agrees else 'FAIL'} {name} {profile}", flush=True)
    assert identity(binary) == binary_identity, "Executable changed during checking"
    assert observed_cases == set(cases), sorted(set(cases) - observed_cases)
    failures = sum(not row["agrees"] for row in records)
    with args.receipt.open("x") as stream:
        json.dump({"format": "he-module-capacity-v1", "binary": binary_identity,
                   "references": references, "records": records, "failures": failures,
                   "reference_deadlocks": deadlocks, "reference_lacks": lacks,
                   "driver_sha256": identity(Path(__file__).resolve())["sha256"],
                   "fixtures_sha256": {str(path.relative_to(work)): identity(path)["sha256"]
                                       for path in sorted(work.rglob("*.metta"))}},
                  stream, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"{len(records) - failures}/{len(records)} module capacity controls agree")
    return bool(failures)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--reference":
        reference_worker(sys.argv[2], Path(sys.argv[3]))
    else:
        raise SystemExit(main())
