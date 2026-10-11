#!/usr/bin/env python3
"""Check initialization effects, runner scopes, source changes and nested loads.

The reference is an existing HE Python installation. Compatibility comparisons
include provider failures before initialization; these are retried upstream.
Default HE intentionally remembers those effects, permits source-change retry,
and preserves successful children. No reference is downloaded or built.

Importing is linking plus naming. Pinned HE does both in one step: a failed
token export leaves the link and the earlier tokens behind, and a module that
is already linked through another module never gives its tokens. A module
whose code fails stays stuck in pinned HE ("Attempt to access module before
loader function has finished"), together with the modules it had loaded.
he-compat follows the repaired reference on the cases in REPAIRED: a failure
publishes and remembers nothing, and the next import is a new attempt. The
pinned observations of those cases are kept as a record of the defects. On
all other cases the two references agree.
"""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile


FIXTURE = Path(__file__).resolve().parents[1] / "tests/he/module_failure_v1"


def attempt(name):
    return f"!(case (import! &self {name}) (((Error $a $b) caught) ($ok loaded)))"


PROGRAMS = {
    "same-run": "\n".join([attempt("run_failure")] * 3),
    "provider-failure": "\n".join([attempt("provider_failure")] * 3),
    "nested-provider-failure": "\n".join([attempt("provider_parent"),
                                           attempt("provider_failure")]),
    "nested-failure": "\n".join([attempt("failed_parent"),
        attempt("retained_child"), attempt("retained_child"),
        "!(collapse (match &self (retained-child-value $x) $x))"]),
    "export-same-run": "\n".join([attempt("export_failure"),
        "! export-only-value", "! export-only-token", attempt("export_failure"),
        "! export-only-value", "! export-only-token", attempt("export_failure")]),
    "export-other-importer": "\n".join([attempt("export_failure"),
        attempt("export_importer"),
        "!(match &self (export-importer-value $x) $x)",
        "!(match &self (export-importer-token $x) $x)",
        attempt("export_failure"), "! export-only-value", "! export-only-token"]),
    "export-reentrant": "\n".join([attempt("export_reentrant"),
        "! reentrant-only-value", attempt("export_reentrant"),
        "! reentrant-only-value"]),
    "transitive-naming": "\n".join([attempt("transitive_importer"),
        "!(match &self (transitive-value $x) $x)",
        "! transitive-export-value", "! transitive-token-7",
        attempt("transitive_tokens"), "! transitive-export-value", "! transitive-token-7",
        attempt("transitive_tokens"), "! transitive-export-value", "! transitive-token-7"]),
    "named-source-stability": "\n".join([
        "!(import! &self memo_target)", "!(import! &self memo_replace)",
        "!(import! &self memo_target)",
        "!(match &self (memo-kind $kind) $kind)", "! memo_py_value"]),
    **{name: f"!(import! &self control)\n!({name})" for name in
       ("separate-runs", "nested-runs", "nested-retry", "source-change", "reuse-slots",
        "export-runs", "source-snapshot", "python-source-change")},
}


IMPORT_NAMING = ("export-same-run", "export-other-importer", "export-reentrant",
                 "export-runs", "transitive-naming")
FAILED_INITIALIZATION = ("same-run", "nested-failure", "separate-runs", "nested-runs",
                         "source-snapshot")
REPAIRED = IMPORT_NAMING + FAILED_INITIALIZATION


# Observations that need a token given as a regular expression. A Python module's
# tokens are whole words here; patterns belong to a separate project. The
# reference answer stays in `expected`. A run that differs from it only by
# leaving such a word unnamed is reported as deferred, never as a pass.
DEFERRED_PATTERN_TOKENS = {"transitive-naming": {"[84]": "[transitive-token-7]"}}


def deferred_only(case, effects, answers, want):
    unnamed = DEFERRED_PATTERN_TOKENS.get(case)
    return bool(unnamed) and effects == want[0] and answers != want[1] and \
        answers == [unnamed.get(answer, answer) for answer in want[1]]


def expected(case, compat):
    if case == "named-source-stability":
        return [], ["[()]", "[()]", "[()]", "[metta]", "[memo_py_value]"]
    if case == "transitive-naming":
        names = ["[transitive-export-value]", "[transitive-token-7]"]
        exports = ["[83]", "[84]"]
        return ([f"transitive-hook-{i}" for i in range(1, 4)],
            ["[loaded]", "[83]", *names, "[loaded]", *exports, "[loaded]", *exports])
    if case == "python-source-change":
        first = ["cached-source-a", "cached-source-a", "cached-refusal"]
        return (first * 3 if compat else first +
                ["cached-source-b", "cached-source-b", "cached-refusal"], ["[()]", "[True]"])
    if case == "source-snapshot":
        return ((["snapshot-a", "snapshot-b", "snapshot-refusal"] +
                 ["snapshot-b", "snapshot-b", "snapshot-refusal"] * 2 if compat else
                 ["snapshot-a", "snapshot-a", "snapshot-refusal",
                  "snapshot-b", "snapshot-b", "snapshot-refusal"]),
                ["[()]", "[True]"])
    if case == "export-same-run":
        unnamed = ["[export-only-value]", "[export-only-token]"]
        return [f"export-hook-{i}" for i in range(1, 4 if compat else 3)], (
            ["[caught]", *unnamed, "[loaded]", "[91]", "[92]", "[loaded]"]
            if compat else ["[caught]", *unnamed, "[caught]", *unnamed, "[caught]"])
    if case == "export-other-importer":
        return [f"export-hook-{i}" for i in range(1, 5 if compat else 4)], (
            ["[caught]", "[loaded]", "[91]", "[92]", "[loaded]", "[91]", "[92]"]
            if compat else ["[caught]", "[loaded]", "[91]", "[92]", "[caught]",
                            "[export-only-value]", "[export-only-token]"])
    if case == "export-reentrant":
        return [f"reentrant-export-{i}" for i in range(1, 4 if compat else 3)], (
            ["[caught]", "[reentrant-only-value]", "[loaded]", "[93]"] if compat else
            ["[caught]", "[reentrant-only-value]", "[caught]", "[reentrant-only-value]"])
    if case == "export-runs":
        return [f"export-hook-{i}" for i in range(1, 4 if compat else 3)], ["[()]",
            "[(caught loaded)]" if compat else "[(caught caught)]"]
    if case == "same-run":
        return ["run-effect"] * (3 if compat else 1), ["[caught]"] * 3
    if case == "provider-failure":
        return ["provider-effect"] * (3 if compat else 1), ["[caught]"] * 3
    if case == "nested-provider-failure":
        return ((["provider-effect"] * 2 + ["provider-parent-effect", "provider-effect"])
                if compat else ["provider-effect", "provider-parent-effect"]), ["[caught]"] * 2
    if case == "nested-failure":
        # A child loaded by a failed parent is unloaded with it in he-compat
        # and runs again when it is imported; the default profiles keep it.
        return (["retained-child-effect", "parent-effect"] +
                (["retained-child-effect"] if compat else []),
                ["[caught]", "[loaded]", "[loaded]", "[(37)]"])
    if case == "nested-retry":
        return ["retained-child-effect", "parent-effect"] + (
            ["retained-child-effect"] if compat else []), ["[()]", "[True]"]
    if case in ("separate-runs", "nested-runs"):
        return ["run-effect"] * (4 if compat else 1), ["[()]", "[True]"]
    if case == "source-change":
        return ["source-a"] * (2 if compat else 1) + ["source-b", "source-c"], ["[()]", "[True]"]
    assert case == "reuse-slots" and not compat
    return ["retained-child-effect"], ["[()]", "[True]"]


def reference_worker(case, directory):
    import hyperon
    import hyperonpy
    from hyperon import Environment, MeTTa

    if case == "identity":
        print(json.dumps({str(path): sha256(path.read_bytes()).hexdigest() for path in
                          (Path(sys.executable), Path(hyperon.__file__), Path(hyperonpy.__file__))}))
        return
    runner = MeTTa(env_builder=Environment.custom_env(working_dir=directory))
    for row in runner.run(PROGRAMS[case]):
        print("[" + ", ".join(map(str, row)) + "]", flush=True)


def observe(command, directory, environment, case, compat, reference=False, check=True):
    process = subprocess.Popen(command, cwd=directory, env=environment,
                               text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate()
        raise RuntimeError(f"Diagnostic timeout: {case}; no language result was inferred")
    result = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
    # Upstream logs each Python provider exception before returning an error
    # atom. Native callbacks return the error without this diagnostic channel.
    allowed_diagnostic = reference and case in ("provider-failure", "nested-provider-failure") and all(
        "Python error loading MeTTa module 'provider_failure'" in line and
        "provider failed before initialization" in line for line in result.stderr.splitlines())
    valid_exit = result.returncode == 0 and (not result.stderr or allowed_diagnostic)
    assert not check or valid_exit, result
    lines = result.stdout.splitlines()
    effects = [line for line in lines if not line.startswith("[")]
    answers = [line for line in lines if line.startswith("[")]
    want = expected(case, compat)
    agrees = valid_exit and (effects, answers) == want
    pending = not reference and valid_exit and deferred_only(case, effects, answers, want)
    assert not check or agrees or pending, (case, compat, result.stdout, want)
    return {"effects": effects, "answers": answers, "agrees": agrees,
            "deferred": "token patterns" if pending else None,
            "stdout": result.stdout, "stderr": result.stderr, "exit_status": result.returncode}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--reference-python", type=Path)
    parser.add_argument("--fixed-reference-python", type=Path)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--observe-only", action="store_true",
                        help="Record every baseline mismatch without claiming qualification")
    parser.add_argument("--case", action="append", choices=PROGRAMS,
                        help="Select a lifecycle package; default runs all controls")
    args = parser.parse_args()
    selected = args.case or list(PROGRAMS)
    binary = args.binary.resolve()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="he-module-failure-", dir=args.work_dir.resolve()))
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONUNBUFFERED="1")
    environment.pop("PYTHONPATH", None)
    records = []

    def directory(name):
        path = work / name
        shutil.copytree(FIXTURE, path, ignore=shutil.ignore_patterns("__pycache__"))
        return path

    reference_identity = None
    references = {}
    if args.reference_python:
        reference_command = [str(args.reference_python.absolute()), str(Path(__file__).resolve()), "--reference"]
        reference_identity = json.loads(subprocess.check_output(
            reference_command + ["identity", str(work)], env=environment, text=True))
        for case in selected:
            if case == "reuse-slots":
                continue
            path = directory("reference-" + case)
            # The pinned observations of REPAIRED are kept as a record of
            # the upstream defects; they are not expectations.
            references[case] = observe(reference_command + [case, str(path)], path,
                                       environment, case, True, reference=True,
                                       check=case not in REPAIRED)

    fixed_identity = None
    fixed_references = {}
    if args.fixed_reference_python:
        fixed_command = [str(args.fixed_reference_python.absolute()), str(Path(__file__).resolve()), "--reference"]
        fixed_identity = json.loads(subprocess.check_output(
            fixed_command + ["identity", str(work)], env=environment, text=True))
        for case in (*REPAIRED, "named-source-stability"):
            if case not in selected:
                continue
            path = directory("fixed-" + case)
            fixed_references[case] = observe(fixed_command + [case, str(path)],
                path, environment, case, True, reference=True)
    fixed_naming = fixed_references.get("transitive-naming")

    for profile in ("he", "he-compat", "extended"):
        compat = profile == "he-compat"
        for route in ("0", "1"):
            for case, source in PROGRAMS.items():
                if case not in selected:
                    continue
                if compat and case == "reuse-slots":
                    continue
                path = directory(f"{profile}-{route}-{case}")
                command = [str(binary), "--lang", "he", "--profile", profile, "-e", source]
                observed = observe(command, path, dict(environment, CETTA_OPEN_EQUATIONS_REFERENCE=route),
                                   case, compat, check=not args.observe_only)
                if observed["deferred"]:
                    pass
                elif not args.observe_only and compat and case in REPAIRED:
                    if case in fixed_references:
                        assert all(observed[key] == fixed_references[case][key]
                                   for key in ("effects", "answers"))
                elif not args.observe_only and compat and case in references:
                    assert all(observed[key] == references[case][key] for key in ("effects", "answers"))
                if not observed["deferred"] and not args.observe_only and not compat and \
                        case == "transitive-naming" and fixed_naming:
                    assert all(observed[key] == fixed_naming[key] for key in ("effects", "answers"))
                records.append({"case": case, "profile": profile, "equation_reference": route, **observed})
                verdict = "PASS" if observed["agrees"] else "DEFER" if observed["deferred"] else "FAIL"
                print(f"{verdict} {profile} route={route} {case}" +
                      (f" ({observed['deferred']})" if observed["deferred"] else ""), flush=True)
    if args.receipt:
        with args.receipt.open("x") as stream:
            json.dump({"format": "he-module-failure-v1", "binary_sha256": sha256(binary.read_bytes()).hexdigest(),
                       "selected_cases": selected,
                       "driver_sha256": sha256(Path(__file__).read_bytes()).hexdigest(),
                       "fixture_sha256": {path.name: sha256(path.read_bytes()).hexdigest()
                                          for path in sorted(FIXTURE.iterdir()) if path.is_file()},
                       "reference_identity": reference_identity, "references": references,
                       "fixed_reference_identity": fixed_identity,
                       "fixed_references": fixed_references,
                       "fixed_transitive_naming": fixed_naming, "records": records},
                      stream, indent=2, sort_keys=True)
            stream.write("\n")
    deferred = sum(bool(record["deferred"]) for record in records)
    failures = sum(not record["agrees"] and not record["deferred"] for record in records)
    print(f"HE initialization failure controls: {len(records)} runs, {failures} mismatches, "
          f"{deferred} deferred to the token-pattern project")
    raise SystemExit(bool(failures))


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--reference":
        reference_worker(sys.argv[2], sys.argv[3])
    else:
        main()
