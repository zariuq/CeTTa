#!/usr/bin/env python3
"""Production invocation capture/replay and stale-evidence controls."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import gslt_invocation as invocation
from check_transform_swi_differential import read_forms, write_form
from check_gslt_through_machine import structural_text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    root = invocation.ROOT
    with tempfile.TemporaryDirectory(prefix="invocation-controls-", dir=root / "runtime") as directory:
        work = Path(directory)
        inputs = []
        for label in ("first", "second"):
            place = work / label
            place.mkdir()
            (place / "helper.metta").write_text(f"(= ({label}-answer) {label})\n")
            path = place / "program with spaces.metta"
            path.write_text(f"!(import! &self ./helper.metta)\n!({label}-answer)\n")
            inputs.append(str(path))
        command = ["env", "--", "CETTA_PETTA_SEARCH_MACHINE=1", binary, "--lang", "petta"]
        corpus = work / "corpus"
        run = subprocess.run(["sh", "tests/support/record_petta_direct_run.sh", str(corpus), str(len(command)),
                              *command, *inputs], cwd=root, text=True, capture_output=True,
                             timeout=30)
        assert run.returncode == 0, run.stderr
        assert run.stdout.splitlines() == ["true", "first", "true", "second"], run.stdout
        manifest, = corpus.glob("*.invocation.json")
        record = invocation.read_record(manifest)
        assert record["command"] == command and record["arguments"] == inputs
        assert record["cwd"] == str(root)
        assert record["process"] == dict(state="exited", returncode=0)
        assert len(record["sources"]) == 2 and len(record["dependencies"]) == 2
        assert not list(corpus.glob("*.metta")), "capture must not concatenate source files"
        joined = work / "first/joined.metta"
        joined.write_text("".join(Path(p).read_text() for p in inputs))
        bad = subprocess.run([*command, str(joined)], cwd=root, text=True,
                             capture_output=True, timeout=30)
        assert bad.stdout != run.stdout, "the control must observe the file-boundary difference"

        # Gate scratch directories can disappear before qualification. Their
        # captured files return at the original paths, then leave no copies.
        captured = record["sources"] + record["dependencies"]
        for item in captured:
            Path(item["path"]).unlink()
        with invocation.restored_inputs(manifest) as restored:
            replay = subprocess.run(restored["command"] + restored["arguments"],
                cwd=restored["cwd"], env=invocation.replay_environment(restored),
                text=True, capture_output=True, timeout=30)
            assert replay.returncode == run.returncode and replay.stdout == run.stdout
        assert all(not Path(item["path"]).exists() for item in captured)

        changed = Path(inputs[0])
        changed.write_text("!(changed)\n")
        try:
            with invocation.restored_inputs(manifest):
                raise AssertionError("stale input was accepted")
        except ValueError as error:
            assert "stale" in str(error)
        assert changed.read_text() == "!(changed)\n", "a live file must never be overwritten"
        changed.unlink()
        blob = corpus / record["sources"][0]["blob"]
        original = blob.read_bytes()
        blob.write_bytes(b"corrupt")
        try:
            with invocation.restored_inputs(manifest):
                raise AssertionError("corrupt capture was accepted")
        except ValueError as error:
            assert "digest" in str(error)
        blob.write_bytes(original)

        record["process"] = dict(state="exited", returncode=1)
        invocation.save_json(manifest, record)
        refused = subprocess.run([sys.executable, "tests/support/check_gslt_through_machine.py",
            binary, "--corpus", str(corpus), "--keep", str(work / "failed-replay")],
            cwd=root, text=True, capture_output=True, timeout=30)
        assert refused.returncode != 0 and "did not complete successfully" in refused.stdout
        record["process"] = dict(state="exited", returncode=0)
        record["source_gate"] = "failed-gate-control"
        record["source_gate_status"] = 1
        invocation.save_json(manifest, record)
        refused = subprocess.run([sys.executable, "tests/support/check_gslt_through_machine.py",
            binary, "--corpus", str(corpus), "--keep", str(work / "failed-gate-replay")],
            cwd=root, text=True, capture_output=True, timeout=30)
        assert refused.returncode != 0 and "source gate did not complete successfully" in refused.stdout
        invocation.main(["finish-gate", str(corpus), "failed-gate-control", "0"])
        assert invocation.read_record(manifest)["source_gate_status"] == 0
        assert invocation.main(["check-gate", str(corpus), "failed-gate-control"]) == 0

        # Strings containing variable-like text and raw guest syntax remain
        # leaves. Parsing deep source does not consume the Python call stack.
        forms = read_forms('(x r##"(import! &self missing) \\" $a"## "literal $b")')
        assert len(forms) == 1 and len(forms[0]) == 3
        assert read_forms("(r##name x)") == [["r##name", "x"]]
        deep_source = "(" * 5000 + "leaf" + ")" * 5000
        deep = read_forms(deep_source)
        assert write_form(deep[0]) == deep_source
        for _ in range(5000):
            deep = deep[0]
        assert deep == ["leaf"]
        assert structural_text(['("literal $x" $a $a)']) == structural_text(['("literal $x" $b $b)'])
        assert structural_text(['("literal $x" $a $a)']) != structural_text(['("literal $y" $b $b)'])
        assert structural_text(['(pair $a $a)']) != structural_text(['(pair $b $c)'])
        assert structural_text(['(pair 1 1.0)']) != structural_text(['(pair 1.0 1)'])
        # A completed source can outlive the machine allowance. The pending
        # marker must stay one protocol value, never two ordinary answers.
        short = work / "short.metta"
        short.write_text('!(+ 1 2)\n')
        short_corpus = work / "short-corpus"
        captured = subprocess.run(["sh", "tests/support/record_petta_direct_run.sh",
            str(short_corpus), str(len(command)), *command, str(short)], cwd=root,
            text=True, capture_output=True, timeout=30)
        assert captured.returncode == 0 and captured.stdout.strip() == "3", captured.stderr
        compare = [sys.executable, "tests/support/check_gslt_through_machine.py", binary,
                   "--corpus", str(short_corpus), "--keep", str(work / "short-runs"),
                   "--timeout", "10", "--turns"]
        pending = subprocess.run([*compare, "1", "1"], cwd=root, text=True,
                                 capture_output=True, timeout=30)
        assert pending.returncode != 0 and "not-ended" in pending.stdout, pending.stdout + pending.stderr
        complete = subprocess.run([*compare, "1000", "1"], cwd=root, text=True,
                                  capture_output=True, timeout=30)
        assert complete.returncode == 0, complete.stdout + complete.stderr
        missing = work / "dynamic.metta"
        missing.write_text('(= (load $path) (import! &self $path))\n')
        pending, data = invocation.make_record(corpus, command, [str(missing)])
        assert data["unresolved_imports"]
        try:
            with invocation.restored_inputs(pending):
                raise AssertionError("unrecorded import context was accepted")
        except ValueError as error:
            assert "imports" in str(error)
    print("PASS: exact argv and file boundaries, import origins, replay, stale/corrupt controls and deep/raw source")


if __name__ == "__main__":
    main()
