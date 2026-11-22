#!/usr/bin/env python3
"""Production invocation capture/replay and stale-evidence controls."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import gslt_invocation as invocation
from check_transform_swi_differential import read_forms, write_form


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    root = invocation.ROOT
    with tempfile.TemporaryDirectory(prefix="invocation-controls-", dir=root / "runtime") as directory:
        work = Path(directory)

        # A replay creates files beside the original documents to preserve
        # relative imports. Another snapshot must wait for their removal,
        # without ignoring any potentially authored library filename.
        context_root = work / "context-root"
        library = context_root / "lib"
        library.mkdir(parents=True)
        authored = library / "authored.metta"
        authored.write_text('(= (authored) stable)\n')
        context_source = context_root / "runtime/source.metta"
        context_source.parent.mkdir()
        context_source.write_text('!(quote stable)\n')
        context_manifest, context_record = invocation.make_record(work / "context-corpus",
            [binary], [str(context_source)], root=context_root)
        context_probe = (
            'import fcntl,json,sys;from pathlib import Path;import gslt_invocation as i;'
            'root=Path(sys.argv[1]);mode=sys.argv[2];'
            'stream=(root/"runtime/gslt-invocation-context.lock").open("a+b");'
            '\nif mode=="nonblocking":\n'
            ' try: fcntl.flock(stream,fcntl.LOCK_EX|fcntl.LOCK_NB)\n'
            ' except BlockingIOError: sys.exit(73)\n'
            'else:\n'
            ' print("ready",flush=True)\n'
            ' print(json.dumps(i.context_identity(root)),flush=True)\n')
        probe_environment = dict(os.environ, PYTHONPATH=str(root / "tests/support"))
        with invocation.restored_inputs(context_manifest):
            transient = library / ".gslt-machine-control.metta"
            transient.write_text('!(quote administrative)\n')
            blocked = subprocess.run([sys.executable, "-c", context_probe,
                str(context_root), "nonblocking"], env=probe_environment, timeout=30)
            assert blocked.returncode == 73, "replay must own the context lock"
            observer = subprocess.Popen([sys.executable, "-c", context_probe,
                str(context_root), "snapshot"], env=probe_environment, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                assert observer.stdout.readline().strip() == "ready"
                assert observer.poll() is None, "snapshot bypassed active replay"
            finally:
                transient.unlink()
        observed, errors = observer.communicate(timeout=30)
        assert observer.returncode == 0, errors
        assert json.loads(observed) == json.loads(json.dumps(context_record["import_context"]))
        authored.write_text('(= (authored) changed)\n')
        try:
            with invocation.restored_inputs(context_manifest):
                raise AssertionError("changed library was accepted after serialization")
        except ValueError as error:
            assert "context is stale" in str(error)

        def replay_output(captured, expected):
            manifest, = captured.glob("*.invocation.json")
            with invocation.restored_inputs(manifest) as record:
                invocation.require_completed(record)
                result = subprocess.run(record["command"] + record["arguments"],
                    cwd=record["cwd"], env=invocation.replay_environment(record),
                    capture_output=True, timeout=30)
            if isinstance(expected, str):
                expected = expected.encode()
            assert result.returncode == 0 and result.stdout == expected, result

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
        def refused(captured, message):
            try:
                replay_output(captured, b"")
                raise AssertionError("an unqualified invocation entered replay")
            except ValueError as error:
                assert message in str(error), error

        refused(corpus, "source invocation did not complete successfully")
        record["process"] = dict(state="exited", returncode=0)
        record["source_gate"] = "failed-gate-control"
        record["source_gate_status"] = 1
        invocation.save_json(manifest, record)
        refused(corpus, "source gate did not complete successfully")
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
        missing = work / "dynamic.metta"
        missing.write_text('(= (load $path) (import! &self $path))\n')
        pending, data = invocation.make_record(corpus, command, [str(missing)])
        assert data["unresolved_imports"]
        try:
            with invocation.restored_inputs(pending):
                raise AssertionError("unrecorded import context was accepted")
        except ValueError as error:
            assert "imports" in str(error)

        # A runtime file input is opaque data, even when named on the guest's
        # argv. Its bytes must be restored and checked without parsing it as
        # another MeTTa document.
        data_file = work / "opaque input.data"
        payload = b'opaque input ) ( { " \\q\n\x00\xff'
        data_file.write_bytes(payload)
        data_program = work / "read-data.metta"
        path_literal = json.dumps(str(data_file), ensure_ascii=False)
        data_program.write_text('!(import-as! &self (library fs) input)\n'
            f'!(input:read-text {path_literal})\n'
            '!(import! &self (library fs))\n'
            f'!(fs:read-text {path_literal})\n'
            f'!(let $text (fs:read-text {path_literal}) '
            '(let $_ (println! $text) (empty)))\n')
        data_corpus = work / "data-corpus"
        data_arguments = ["--lang", "petta", str(data_program), "--", str(data_file)]
        recorded = subprocess.run([sys.executable, "tests/support/run_gslt_program.py",
            "--data-file", str(data_file), "--", binary, *data_arguments], cwd=root,
            env=dict(os.environ, CETTA_GSLT_RECORD_DIR=str(data_corpus)),
            capture_output=True, timeout=30)
        assert recorded.returncode == 0 and b"opaque input" in recorded.stdout, recorded.stderr
        data_manifest, = data_corpus.glob("*.invocation.json")
        data_record = invocation.read_record(data_manifest)
        assert data_record["arguments"] == data_arguments
        assert len(data_record["sources"]) == 1 and len(data_record["data_inputs"]) == 1
        assert data_record["data_inputs"][0]["kind"] == "file"
        # CeTTa also accepts -- before the source filename. That separator
        # must not hide the source or turn subsequent guest values into code.
        _, leading_record = invocation.make_record(work / "leading-corpus", [binary],
            ["--lang", "petta", "--", str(data_program), str(data_file)], data_inputs=[data_file])
        assert len(leading_record["sources"]) == 1
        assert leading_record["sources"][0]["argument"] == 3

        # Data roles belong to argument occurrences. An opaque file need not
        # follow a separator, and a file can be both code and a later value.
        positional_corpus = work / "positional-data-corpus"
        positional_args = ["--lang", "petta", str(data_program), str(data_file)]
        positional = subprocess.run([sys.executable, "tests/support/run_gslt_program.py",
            "--data-argument", "3", "--", binary, *positional_args], cwd=root,
            env=dict(os.environ, CETTA_GSLT_RECORD_DIR=str(positional_corpus)),
            capture_output=True, timeout=30)
        assert positional.returncode == 0 and positional.stdout == recorded.stdout, positional.stderr
        positional_manifest, = positional_corpus.glob("*.invocation.json")
        positioned = invocation.read_record(positional_manifest)
        assert positioned["arguments"] == positional_args and positioned["data_arguments"] == [3]
        assert [item["argument"] for item in positioned["sources"]] == [2]
        assert positioned["data_inputs"][0]["argument"] == 3
        assert not positioned["unresolved_imports"], "opaque data must not be parsed for imports"
        replay_output(positional_corpus, recorded.stdout)
        _, declared = invocation.make_record(work / "declared-data-corpus", [binary], positional_args,
                                             data_inputs=[data_file])
        assert [item["argument"] for item in declared["sources"]] == [2]
        assert not declared["unresolved_imports"]
        self_data = work / "self as data.metta"
        self_data.write_text('!(import! &self system)\n!(system:arg 1)\n')
        self_corpus = work / "self-data-corpus"
        self_args = ["--lang", "petta", str(self_data), "--", str(self_data)]
        self_run = subprocess.run([sys.executable, "tests/support/run_gslt_program.py",
            "--data-argument", "4", "--", binary, *self_args], cwd=root,
            env=dict(os.environ, CETTA_GSLT_RECORD_DIR=str(self_corpus)),
            capture_output=True, timeout=30)
        assert self_run.returncode == 0 and str(self_data).encode() in self_run.stdout, self_run.stderr
        self_manifest, = self_corpus.glob("*.invocation.json")
        self_record = invocation.read_record(self_manifest)
        assert [item["argument"] for item in self_record["sources"]] == [2]
        assert self_record["data_inputs"][0]["argument"] == 4
        assert self_record["sources"][0]["path"] == self_record["data_inputs"][0]["path"]
        replay_output(self_corpus, self_run.stdout)
        for invalid in (-1, len(self_args)):
            try:
                invocation.make_record(work / "bad-position", [binary], self_args, data_arguments=[invalid])
                raise AssertionError("invalid file-argument position was accepted")
            except ValueError as error:
                assert "index" in str(error)
        data_file.unlink()
        with invocation.restored_inputs(data_manifest) as restored:
            assert data_file.read_bytes() == payload
            replay = subprocess.run(restored["command"] + restored["arguments"],
                cwd=restored["cwd"], env=invocation.replay_environment(restored),
                capture_output=True, timeout=30)
            assert replay.returncode == 0 and replay.stdout == recorded.stdout
        assert not data_file.exists()
        values = work / "values.metta"
        values.write_text('!(quote ("literal $name" 1 1.0 True False (cons a (b c)) (nullary) ()))\n'
                          '!(quote (pair $a $a $b))\n!(quote $a)\n!(quote $a)\n'
                          '!(quote (pm:no-end ordinary-data))\n'
                          '!(quote (pm:uncaught ordinary-data))\n'
                          '!(quote (Error ordinary-data context))\n')
        value_corpus = work / "value-corpus"
        original = subprocess.run([sys.executable, "tests/support/run_gslt_program.py", "--",
            binary, "--lang", "petta", str(values)], cwd=root,
            env=dict(os.environ, CETTA_GSLT_RECORD_DIR=str(value_corpus)),
            capture_output=True, text=True, timeout=30)
        assert original.returncode == 0, original.stderr
        replay_output(value_corpus, original.stdout)
        data_file.write_bytes(b"changed input")
        try:
            with invocation.restored_inputs(data_manifest):
                raise AssertionError("changed runtime data was accepted")
        except ValueError as error:
            assert "stale" in str(error)
        assert data_file.read_bytes() == b"changed input"

        # Equal bytes do not make retargeted path resolution the same input.
        first, second, alias = work / "data-first", work / "data-second", work / "data-link"
        first.write_bytes(payload)
        second.write_bytes(payload)
        alias.symlink_to(first)
        link_manifest, _ = invocation.make_record(work / "link-corpus", [binary],
            [str(data_program)], data_inputs=[alias])
        alias.unlink()
        alias.symlink_to(second)
        try:
            with invocation.restored_inputs(link_manifest):
                raise AssertionError("retargeted data input was accepted")
        except ValueError as error:
            assert "resolution is stale" in str(error)

        absent = work / "absent-data"
        absent_manifest, absent_record = invocation.make_record(work / "absent-corpus", [binary],
            [str(data_program)], data_inputs=[absent])
        assert absent_record["data_inputs"][0]["kind"] == "absent"
        with invocation.restored_inputs(absent_manifest):
            assert not absent.exists()
        absent.write_bytes(payload)
        try:
            with invocation.restored_inputs(absent_manifest):
                raise AssertionError("snapshot-relative absence was promoted after file creation")
        except ValueError as error:
            assert "absent input is now present" in str(error)

        # Replays side by side: a record that must find an input absent never
        # shares a batch with one whose replay restores it; records agreeing
        # on every input they declare share one.
        shared = work / "batch-shared"
        shared.write_bytes(payload)
        present_manifest, _ = invocation.make_record(work / "present-corpus", [binary],
            [str(data_program)], data_inputs=[shared])
        twin_manifest, _ = invocation.make_record(work / "twin-corpus", [binary],
            [str(data_program)], data_inputs=[shared])
        shared.unlink()
        absent_twin_manifest, _ = invocation.make_record(work / "absent-twin-corpus", [binary],
            [str(data_program)], data_inputs=[shared])
        batches = invocation.replay_batches(
            [present_manifest, absent_twin_manifest, twin_manifest])
        assert batches == [[present_manifest, twin_manifest], [absent_twin_manifest]], batches

        # A batch restores a generated input its records lack once, keeps it
        # through the batch, and removes it after.
        generated = work / "batch-generated.metta"
        generated.write_text('!(quote batched)\n')
        first_manifest, _ = invocation.make_record(work / "batch-first", [binary], [str(generated)])
        second_manifest, _ = invocation.make_record(work / "batch-second", [binary], [str(generated)])
        generated.unlink()
        with invocation.restored_batch([first_manifest, second_manifest]) as records:
            assert all(isinstance(record, dict) for record in records), records
            assert generated.read_text() == '!(quote batched)\n'
        assert not generated.exists()

        # Effects precede buffered CLI answers. Keep every guest line, even
        # a line whose text happens to equal the administrative marker.
        effects = work / "effects.metta"
        effects.write_text('!(let $_ (println! @@START) first-value)\n'
                           '!(let $_ (println! second-effect) second-value)\n')
        effect_corpus = work / "effect-corpus"
        original = subprocess.run(["sh", "tests/support/record_petta_direct_run.sh",
            str(effect_corpus), str(len(command)), *command, str(effects)], cwd=root,
            capture_output=True, text=True, timeout=30)
        assert original.returncode == 0
        assert original.stdout.splitlines() == ["@@START", "second-effect", "first-value", "second-value"]
        replay_output(effect_corpus, original.stdout)
    print("PASS: exact argv and file boundaries, import origins, replay, stale/corrupt controls and deep/raw source")
    print("PASS: replay source neighbours serialize context snapshots; authored changes still invalidate them")
    print("PASS: opaque file data, byte restoration, changed data, path resolution and recorded absence")
    print("PASS: immediate effects, buffered answers and guest marker text remain observable")
    print("PASS: direct replay preserves quoted values and host effects")


if __name__ == "__main__":
    main()
