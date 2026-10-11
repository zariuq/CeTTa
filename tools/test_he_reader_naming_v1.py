#!/usr/bin/env python3
"""Compare HE read-time names and literal spellings with reference runners."""

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from metta_result_observation import output_observation

CASES = {
    "bound_reference": "!(bind! &x 7) !(quote &x)",
    "quoted_expression": '!(bind! &x 7) !(quote (pair &x "&x"))',
    "unknown_reference": "!(quote &unknown)",
    "whole_word": "!(bind! &x 7) !(quote (&xfoo x&x &x))",
    "read_before_binding": "(= (old) &x) !(bind! &x 7) !(old)",
    "read_after_binding": "!(bind! &x 7) (= (new) (quote &x)) !(new)",
    "single_substitution_symbol": "!(bind! &x y) !(bind! y 5) ! &x",
    "single_substitution_expression": "!(bind! &x (pair y)) !(bind! y 5) ! &x",
    "prior_binding": "!(bind! y 5) !(bind! &x y) ! &x",
    "bound_argument": "!(bind! &x 7) !(case (bind! &x 8) (((Error $c $r) $r))) ! &x",
    "self_argument": "!(case (bind! &self 7) (((Error $c $r) $r)))",
    "missing_module": "!(import! &self syn)",
    "self_in_declaration": "(= (owner) &self) !(let $v (owner) (get-metatype $v))",
    "string_bytes": '!(bind! &x 7) !(quote ("&x" "np.pi" "&self"))',
    "dotted_symbols": "! np.pi ! np:pi ! mork.foo ! mork:foo",
    "different_symbols": "!(== (quote np.pi) (quote np:pi))",
    "dotted_expression": "!(quote (np.pi np:pi a.b/c 1.25))",
    "dotted_variables": "!(quote ($a.b $a:b))",
    "different_variables": "(= (reader-pair $a.b $a:b) (pair $a.b $a:b)) !(reader-pair 1 2)",
    "paths_and_punctuation": "! .foo ! foo..bar ! foo.metta ! folder/file.foo ! a-b.c_d",
    "dotted_reference": "!(bind! &a.b 11) !(quote &a.b)",
    "included_scope": '!(bind! &reader-number 7) !(include support:he_reader_naming_included) !(included-number) !(included-dot) !(let $v (included-owner) (get-metatype $v))',
    "module_scope": '!(import! &self support:he_reader_naming_owner) !(let $v (reader-owner) (== $v &self)) !(reader-owned-dot) !(reader-owned-value)',
    "file": None,
}


def identity(path):
    data = Path(path).read_bytes()
    return {"sha256": sha256(data).hexdigest(), "bytes": len(data)}


def reference_worker(name):
    import hyperon
    import hyperonpy
    from hyperon import Environment, MeTTa
    if name == "identity":
        print(json.dumps({"python": identity(sys.executable),
                          "package": identity(hyperon.__file__),
                          "extension": identity(hyperonpy.__file__)}))
        return
    code = CASES[name]
    if code is None:
        code = (ROOT / "tests/test_he_reader_naming.metta").read_text()
    runner = MeTTa(env_builder=Environment.custom_env(working_dir=str(ROOT / "tests")))
    for answers in runner.run(code):
        print("[" + ", ".join(map(str, answers)) + "]", flush=True)


def run(command):
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONUNBUFFERED="1")
    env.pop("PYTHONPATH", None)
    process = subprocess.Popen(command, cwd=ROOT / "tests", env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate()
        raise RuntimeError("Unfinished diagnostic; no language result inferred")
    if process.returncode or stderr:
        raise RuntimeError(f"Unexpected process failure: {process.returncode}\n{stderr}\n{stdout}")
    return stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--reference-python", action="append", required=True, type=Path)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--case", action="append", choices=CASES)
    args = parser.parse_args()
    binary = args.binary.absolute()
    binary_identity = identity(binary)
    rows = []
    references = []
    for python in args.reference_python:
        # Preserve the interpreter's venv path, including its symlink.
        worker = [str(python.absolute()), "-I", "-B", str(Path(__file__).resolve()), "--reference"]
        reference_identity = json.loads(run(worker + ["identity"]))
        references.append(reference_identity)
        for name in args.case or CASES:
            expected = run(worker + [name])
            for profile in ("he", "he-compat", "extended"):
                source = ("-e", CASES[name]) if CASES[name] is not None else (
                    str(ROOT / "tests/test_he_reader_naming.metta"),)
                actual = run([str(binary), "--lang", "he", "--profile", profile, *source])
                if name == "included_scope" and profile != "he-compat":
                    # Default profiles retain the Unit result of include.
                    lines = expected.splitlines()
                    assert lines[1] == "[]"
                    lines[1] = "[()]"
                    expected_profile = "\n".join(lines) + "\n"
                else:
                    expected_profile = expected
                agrees = output_observation(expected_profile) == output_observation(actual)
                rows.append(dict(case=name, profile=profile, reference_identity=reference_identity,
                                 reference=expected, expected=expected_profile, actual=actual, agrees=agrees))
                print("PASS" if agrees else "FAIL", name, profile, flush=True)
    if identity(binary) != binary_identity:
        raise RuntimeError("Executable identity changed during qualification")
    failures = sum(not row["agrees"] for row in rows)
    if args.receipt:
        with args.receipt.open("x") as stream:
            json.dump(dict(format="he-reader-naming-v1", binary=binary_identity,
                references=references, cases=CASES, records=rows, failures=failures,
                driver=identity(__file__), fixtures={str(p.relative_to(ROOT)): identity(p) for p in (
                    ROOT / "tests/test_he_reader_naming.metta",
                    ROOT / "tests/support/he_reader_naming_included.metta",
                    ROOT / "tests/support/he_reader_naming_owner.metta")}), stream, indent=2)
    print(f"{len(rows) - failures}/{len(rows)} reader naming controls agree")
    return int(failures != 0)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--reference":
        reference_worker(sys.argv[2])
    else:
        raise SystemExit(main())
