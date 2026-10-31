#!/usr/bin/env python3
"""Adversarial self-tests for the runtime-readiness evidence model."""

from __future__ import annotations

import json
import csv
import hashlib
import io
import importlib.util
import os
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path
from unittest.mock import patch

from bench_d4_progress import census_classification
from cetta_main_readiness import readiness_commands, petta_oracle_identity, source_identity
from check_precise_vocabulary import WORD, pinned_reference_field
from rhocalc_bench_provenance import benchmark_provenance
from cetta_readiness_ladders import (
    primary_contract,
    realized_witness_ids,
    required_ladder_witness_ids,
    scheduled_cases,
)
from cetta_readiness_model import (
    ReadinessModelError,
    calibration_subject,
    counter_contract,
    evidence_key,
    growth_verdict,
    load_property_manifest,
    load_frozen_baseline,
    memory_rate_verdict,
    missing_frontier_witness_ids,
    paired_growth_verdict,
    qualification_verdict,
    required_frontier_witness_ids,
    resolve_source_anchors,
    route_contract,
    terminate_process_session,
    working_tree_content_identity,
)


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "benchmarks/main_readiness_properties.json"
PASS_COUNT = 0


def check_frozen_baseline() -> None:
    """Independently assembled archive, Git blobs, binary and runtime library."""
    with tempfile.TemporaryDirectory(prefix="readiness-frozen-", dir=ROOT / "runtime") as tmp:
        directory = Path(tmp)
        (directory / "bin").mkdir()
        (directory / "lib").mkdir()
        binary = directory / "bin/cetta"
        binary.write_bytes(b"frozen executable fixture")
        contents = {"src/eval.c": b"source fixture\n", "lib/core.metta": b"(Core fixture)\n"}
        (directory / "lib/core.metta").write_bytes(contents["lib/core.metta"])
        sha = lambda value: hashlib.sha256(value).hexdigest()
        commit = "c" * 40
        entries = [dict(path=name, kind="file", bytes=len(value), sha256=sha(value))
                   for name, value in contents.items()]
        tree = b"".join(b"100644 blob " + hashlib.sha1(
            f"blob {len(value)}\0".encode() + value).hexdigest().encode() +
            b"\t" + name.encode() + b"\0" for name, value in contents.items())
        archive = directory / "source.tar"
        with tarfile.open(archive, "w") as stream:
            for name, value in contents.items():
                info = tarfile.TarInfo(name)
                info.size = len(value)
                stream.addfile(info, io.BytesIO(value))
        source = directory / "source.json"
        source.write_text(json.dumps(dict(head=commit, source_files=entries, dependencies={})))
        build = directory / "build.json"
        build.write_text(json.dumps(dict(source_head=commit, sha256=sha(binary.read_bytes()),
                                        runtime_stats=0, build="fixture")))
        receipt = dict(schema="cetta.readiness.frozen-baseline.v2", commit=commit,
                       library_root="lib")
        paths = dict(source_archive=archive, source_manifest=source, binary=binary,
                     build_receipt=build)
        for key, path in paths.items():
            receipt[key] = dict(path=str(path.relative_to(directory)), sha256=sha(path.read_bytes()))
        receipt_path = directory / "receipt.json"

        def write_receipt(value):
            receipt_path.write_text(json.dumps(value))
            return sha(receipt_path.read_bytes())

        digest = write_receipt(receipt)
        check_output = subprocess.check_output

        def source_tree(command, *args, **kwargs):
            if command[:2] == ["git", "ls-tree"]:
                return tree
            return check_output(command, *args, **kwargs)

        with patch("cetta_readiness_model.subprocess.check_output", side_effect=source_tree):
            admitted = load_frozen_baseline(ROOT, receipt_path, digest, commit)
            require(admitted["binary_sha256"] == sha(binary.read_bytes()), "wrong frozen binary")
            report_pass("frozen cohort joins exact archive, Git blobs, build and library")
            pinned = benchmark_provenance(str(binary), receipt=str(receipt_path),
                receipt_sha256=digest, source_commit=commit)
            require(pinned["commit"] == commit and pinned["tree_state"] == "frozen",
                    "rho provenance did not use the checked frozen cohort")
            report_pass("rho provenance uses the frozen cohort")
            with patch("rhocalc_bench_provenance._git_root", return_value=ROOT.parent.parent):
                unknown = benchmark_provenance(str(binary))
                require(unknown["commit"] == "unknown" and unknown["tree_state"] == "nogit",
                        "enclosing workspace was mistaken for a CeTTa checkout")
            report_pass("rho provenance rejects an enclosing workspace identity")

            def rejects(label, value, expected_digest=None):
                current_digest = write_receipt(value)
                try:
                    load_frozen_baseline(ROOT, receipt_path,
                        current_digest if expected_digest is None else expected_digest, commit)
                except ReadinessModelError:
                    report_pass(label)
                else:
                    raise AssertionError(label + " was admitted")

            rejects("frozen receipt rejects wrong caller digest", receipt, "0" * 64)
            rejects("frozen receipt rejects a non-object", [])
            bad = dict(receipt, commit="d" * 40)
            rejects("frozen receipt rejects another source commit", bad)
            binary.write_bytes(b"altered executable fixture")
            rejects("frozen receipt rejects altered binary", receipt)
            binary.write_bytes(b"frozen executable fixture")
            (directory / "lib/core.metta").write_bytes(b"altered runtime library")
            rejects("frozen receipt rejects altered runtime library", receipt)
            (directory / "lib/core.metta").write_bytes(contents["lib/core.metta"])
            original_source = source.read_bytes()
            for label, malformed in [
                ("incomplete source inventory", entries[:1]),
                ("duplicate source inventory", entries + entries[:1]),
                ("non-list source inventory", {}),
                ("malformed source entry", [None]),
            ]:
                source.write_text(json.dumps(dict(head=commit, source_files=malformed, dependencies={})))
                bad = dict(receipt, source_manifest=dict(path="source.json", sha256=sha(source.read_bytes())))
                rejects("frozen receipt rejects " + label, bad)
            source.write_bytes(original_source)
            original_archive = archive.read_bytes()
            with tarfile.open(archive, "w") as stream:
                info = tarfile.TarInfo("src/eval.c")
                info.size = len(contents["src/eval.c"])
                stream.addfile(info, io.BytesIO(contents["src/eval.c"]))
            bad = dict(receipt, source_archive=dict(path="source.tar", sha256=sha(archive.read_bytes())))
            rejects("frozen receipt rejects incomplete archive with a valid digest", bad)
            archive.write_bytes(original_archive)


def check_consolidation_matrix() -> None:
    """Changing a reference must never fall back to historical timing numbers."""
    with tempfile.TemporaryDirectory(prefix="readiness-matrix-", dir=ROOT / "runtime") as tmp:
        directory = Path(tmp)
        with (ROOT / "benchmarks/rhocalc_performance_baseline_9050a2d.tsv").open() as file:
            expected = list(csv.DictReader(file, delimiter="\t"))
        rows = [dict(row, median_s="1.01", baseline_median_s="1.0",
                     baseline_commit="c" * 40, baseline_binary_sha256="a" * 64) for row in expected]

        def check(label, changes, should_pass):
            modified = [dict(row, **changes) for row in rows]
            for witness in {row["witness"] for row in rows}:
                with (directory / (witness + "_current.csv")).open("w") as file:
                    writer = csv.DictWriter(file, fieldnames=list(modified[0]))
                    writer.writeheader()
                    writer.writerows(row for row in modified if row["witness"] == witness)
            result = subprocess.run([sys.executable, str(ROOT / "scripts/rhocalc_benchmark_regression.py"),
                "--matrix-dir", str(directory), "--require-paired", "--max-ratio", "1.05", "--min-slack", "0",
                "--baseline-commit", "c" * 40, "--baseline-sha256", "a" * 64], capture_output=True, text=True)
            require((result.returncode == 0) == should_pass, label + ": " + result.stderr)
            report_pass(label)

        check("consolidation matrix accepts pinned paired observations", {}, True)
        check("consolidation matrix rejects a six percent regression", {"median_s": "1.06"}, False)
        check("consolidation matrix rejects missing paired timings", {"baseline_median_s": ""}, False)
        check("consolidation matrix rejects another binary", {"baseline_binary_sha256": "b" * 64}, False)
        check("consolidation matrix rejects another commit", {"baseline_commit": "d" * 40}, False)
        check("consolidation matrix rejects zero timings", {"median_s": "0"}, False)
        check("consolidation matrix rejects nonfinite timings", {"median_s": "nan"}, False)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def report_pass(message: str) -> None:
    global PASS_COUNT
    PASS_COUNT += 1
    print(f"PASS {message}")


def session_pids(session_id: int) -> list[int]:
    output = subprocess.check_output(
        ["ps", "-eo", "pid=,sid="], text=True
    )
    return [
        int(fields[0])
        for line in output.splitlines()
        if len(fields := line.split()) == 2 and int(fields[1]) == session_id
    ]


def check_session_cleanup(command: list[str], *, leader_exits: bool) -> None:
    process = subprocess.Popen(command, start_new_session=True)
    try:
        if leader_exits:
            process.wait(timeout=2)
        time.sleep(0.1)
        require(session_pids(process.pid), "session-cleanup fixture has no process")
        terminate_process_session(process)
        require(not session_pids(process.pid), "readiness process session survived cleanup")
    finally:
        terminate_process_session(process)


def process_rows() -> list[tuple[int, int, int]]:
    output = subprocess.check_output(
        ["ps", "-eo", "pid=,ppid=,sid="], text=True
    )
    return [
        tuple(map(int, fields))
        for line in output.splitlines()
        if len(fields := line.split()) == 3
    ]


def descendants_of(parent: int) -> list[tuple[int, int, int]]:
    descendants = {parent}
    rows = process_rows()
    changed = True
    while changed:
        changed = False
        for pid, ppid, _sid in rows:
            if ppid in descendants and pid not in descendants:
                descendants.add(pid)
                changed = True
    return [row for row in rows if row[0] in descendants]


def check_nested_session_cleanup() -> None:
    process = subprocess.Popen(
        ["bash", "-c", "setsid bash -c 'sleep 300' & wait"],
        start_new_session=True,
    )
    try:
        time.sleep(0.1)
        nested = descendants_of(process.pid)
        nested_pids = {pid for pid, _ppid, _sid in nested}
        require(
            any(sid != process.pid for _pid, _ppid, sid in nested),
            "nested-session fixture did not create a private child session",
        )
        terminate_process_session(process)
        live_pids = {pid for pid, _ppid, _sid in process_rows()}
        require(
            not (nested_pids & live_pids),
            "nested readiness process session survived cleanup",
        )
    finally:
        terminate_process_session(process)


def run_scale_fixture(environment: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["./scripts/bench_space_scale_ladder.sh", "100 1000", "1"],
        cwd=ROOT,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def check_scale_frontier_exit_contract() -> None:
    runtime = ROOT / "runtime"
    runtime.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="scale-frontier-contract-", dir=runtime
    ) as scratch:
        fake_backend = Path(scratch) / "fake-backend"
        fake_transfer = Path(scratch) / "fake-transfer"
        evidence_dir = Path(scratch) / "evidence"
        canonical_log = (
            ROOT / "runtime/bench_space_scale_ladder/generator_materialized_100.log"
        )
        canonical_before = (
            canonical_log.read_bytes() if canonical_log.exists() else None
        )
        fake_backend.write_text(
            "#!/usr/bin/env bash\n"
            "set -eu\n"
            "mode=$1; count=$2; scenario=${4:-suite_total}\n"
            "if [ \"$mode\" = prepare-act ]; then\n"
            "  test \"$(wc -l <\"$CETTA_BENCH_CORPUS_PATH\")\" = \"$count\"\n"
            "  test \"$(head -1 \"$CETTA_BENCH_CORPUS_PATH\")\" = '(friend sam 0)'\n"
            "  test \"$(tail -1 \"$CETTA_BENCH_CORPUS_PATH\")\" = \"(friend sam $((count - 1)))\"\n"
            "  printf fake-act >\"$CETTA_BENCH_ACT_PATH\"\n"
            "  exit 0\n"
            "fi\n"
            "fixture_mode=${CETTA_SCALE_FIXTURE_MODE:-pass}\n"
            "if [ \"${CETTA_SCALE_FIXTURE_LITMUS_TIMEOUT:-0}\" = 1 ] && "
            "[ \"${CETTA_BENCH_MATERIALIZED_RANGE:-0}\" = 1 ]; then\n"
            "  sleep 5\n"
            "fi\n"
            "if [ \"${CETTA_BENCH_MATERIALIZED_RANGE:-0}\" = 1 ]; then fixture_mode=pass; fi\n"
            "if [ \"$fixture_mode\" = fail ]; then exit 7; fi\n"
            "if [ \"$fixture_mode\" = timeout ] && "
            "[ \"$count\" = 1000 ]; then sleep 5; fi\n"
            "pathmap=0; case \"$mode\" in pathmap) pathmap=$count;; esac\n"
            "rows=$count; [ \"$scenario\" = suite_total ] && rows=$((count - 1))\n"
            "printf 'kind\\tmode\\tscenario\\tfact_count\\tmatch_rounds\\tcount\\ttime_s\\trss_kb\\tscenario_ns\\tterm_universe_inserts\\tterm_universe_blob_bytes\\tspace_atom_id_capacity_bytes_peak\\tpathmap_direct_store\\tmork_add_call\\tmork_add_batch_items\\n'\n"
            "printf 'backend\\t%s\\t%s\\t%s\\t1\\t%s\\t0.01\\t%s\\t%s\\t%s\\t%s\\t%s\\t%s\\t0\\t0\\n' "
            "\"$mode\" \"$scenario\" \"$count\" \"$rows\" "
            "\"$((1000 + count))\" \"$((count * 1000))\" "
            "\"$((count + 1))\" \"$((count + 2))\" \"$((count + 3))\" \"$pathmap\"\n",
            encoding="utf-8",
        )
        fake_transfer.write_text(
            "#!/usr/bin/env bash\n"
            "set -eu\n"
            "case_id=$1; count=$2; scenario=${4:-suite_total}\n"
            "if [ \"${CETTA_SCALE_FIXTURE_MODE:-pass}\" = fail ]; then exit 7; fi\n"
            "if [ \"${CETTA_SCALE_FIXTURE_MODE:-pass}\" = timeout ] && "
            "[ \"$count\" = 1000 ]; then sleep 5; fi\n"
            "pathmap=0; batch=0\n"
            "case \"$case_id\" in\n"
            "  native-to-pathmap|pathmap-to-native|pathmap-to-mork-live|mork-live-to-pathmap) pathmap=$count;;\n"
            "  native-to-mork-live) batch=$count;;\n"
            "esac\n"
            "printf 'kind\\tcase_id\\tsource_kind\\ttarget_kind\\troute_class\\tscenario\\tfact_count\\tmatch_rounds\\tcount\\ttime_s\\trss_kb\\tscenario_ns\\tterm_universe_inserts\\tterm_universe_blob_bytes\\tspace_atom_id_capacity_bytes_peak\\tpathmap_direct_store\\tmork_add_call\\tmork_add_batch_items\\n'\n"
            "printf 'transfer\\t%s\\tnative\\tpathmap\\tmaterialized-shim\\t%s\\t%s\\t1\\t%s\\t0.01\\t%s\\t%s\\t%s\\t%s\\t%s\\t%s\\t0\\t%s\\n' "
            "\"$case_id\" \"$scenario\" \"$count\" \"$count\" "
            "\"$((1000 + count))\" \"$((count * 1000))\" "
            "\"$((count + 1))\" \"$((count + 2))\" \"$((count + 3))\" "
            "\"$pathmap\" \"$batch\"\n",
            encoding="utf-8",
        )
        for fixture in (fake_backend, fake_transfer):
            fixture.chmod(0o755)
        base_environment = os.environ.copy()
        base_environment.update(
            {
                "CETTA_BENCH_BACKEND_SCRIPT": str(fake_backend),
                "CETTA_BENCH_TRANSFER_SCRIPT": str(fake_transfer),
                "CETTA_SCALE_RUNTIME_DIR": str(evidence_dir),
                # This checks timeout classification, not subprocess launch
                # latency. The deliberate timeout fixtures sleep five seconds.
                "CETTA_SCALE_CASE_TIMEOUT_S": "1",
                "CETTA_GENERATOR_LITMUS_FACT_COUNT": "100",
                # These attempted weakenings must be ignored by qualification.
                "BACKEND_MODES_STR": "",
                "TRANSFER_CASES_STR": "",
                "CETTA_ADMITTED_10M_CASES": "",
                "CETTA_SCALE_MAX_TIME_EXPONENT": "99",
                "CETTA_SCALE_MAX_RSS_EXPONENT": "99",
                "CETTA_GENERATOR_LITMUS_ADMISSION": "required",
            }
        )
        failure_environment = dict(base_environment)
        failure_environment["CETTA_SCALE_FIXTURE_MODE"] = "fail"
        failed = run_scale_fixture(failure_environment)
        require(failed.returncode != 0, "scale frontier accepted execution failure")
        require(
            "SPACE_FRONTIER_STATUS=failed" in failed.stdout
            and "SPACE_FRONTIER_FAILURES=1" in failed.stdout,
            "scale frontier did not preserve genuine failure evidence",
        )
        report_pass("scale frontier rejects genuine execution failures")

        timeout_environment = dict(base_environment)
        timeout_environment["CETTA_SCALE_FIXTURE_MODE"] = "timeout"
        timed_out = run_scale_fixture(timeout_environment)
        require(
            timed_out.returncode != 0
            and "SPACE_FRONTIER_STATUS=failed" in timed_out.stdout
            and "SPACE_FRONTIER_TIMEOUTS=1" in timed_out.stdout,
            "required frontier timeout did not fail closed",
        )
        report_pass("every frontier case is required and times out fail closed")

        passing_environment = dict(base_environment)
        passing_environment["CETTA_SCALE_FIXTURE_MODE"] = "pass"
        passed = run_scale_fixture(passing_environment)
        require(
            passed.returncode == 0,
            "passing frontier fixture failed:\n" + passed.stdout,
        )
        require(
            "SPACE_FRONTIER_REQUIRED_CASES=12" in passed.stdout
            and "SPACE_FRONTIER_BASE_CASES=12" in passed.stdout
            and "SPACE_FRONTIER_EXECUTED_CASES=12" in passed.stdout
            and "SPACE_FRONTIER_STATUS=passed" in passed.stdout,
            "complete frontier execution did not report all required cases",
        )
        report_pass("fixed twelve-case frontier emits measurement evidence")

        manifest = load_property_manifest(MANIFEST)
        realized = {
            line.split("=", 1)[1]
            for line in passed.stdout.splitlines()
            if line.startswith("SPACE_FRONTIER_WITNESS=")
        }
        require(
            not missing_frontier_witness_ids(manifest, realized, 1000),
            "passing frontier omitted a manifest-claimed witness",
        )
        removed_witness = next(
            iter(required_frontier_witness_ids(manifest, 1000))
        )
        require(
            missing_frontier_witness_ids(
                manifest, realized - {removed_witness}, 1000
            )
            == [removed_witness],
            "frontier accepted a fabricated execution-derived witness set",
        )
        report_pass("manifest frontier claims are checked against realized IDs")

        litmus_environment = dict(passing_environment)
        litmus_environment.update(
            {
                "CETTA_SCALE_FIXTURE_LITMUS_TIMEOUT": "1",
                "CETTA_GENERATOR_LITMUS_TIMEOUT_S": "1",
            }
        )
        litmus = run_scale_fixture(litmus_environment)
        require(
            litmus.returncode == 0
            and "GENERATOR_LITMUS_STATUS=observed-timeout" in litmus.stdout
            and "GENERATOR_LITMUS_AUTHORITATIVE_READINESS=0" in litmus.stdout,
            "observational generator timeout blocked readiness",
        )
        report_pass("generator capability timeout remains observational")
        canonical_after = (
            canonical_log.read_bytes() if canonical_log.exists() else None
        )
        require(
            canonical_after == canonical_before
            and (evidence_dir / "generator_materialized_100.log").is_file(),
            "frontier fixture overwrote authoritative benchmark evidence",
        )
        report_pass("frontier fixtures isolate their generated evidence")


def check_petta_reference_identity() -> None:
    spec = importlib.util.spec_from_file_location(
        'petta_reference_check', ROOT / 'tools/check_petta_release_reference.py')
    checker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checker)
    with tempfile.TemporaryDirectory(prefix='readiness-petta-', dir=ROOT / 'runtime') as tmp:
        root = Path(tmp)
        contents = {'run.sh': '#!/bin/sh\n', 'src/eval.pl': 'eval(value).\n',
                    'lib/init.metta': '(Library)\n', 'examples/probe.metta': '!(probe)\n',
                    'tests/probe.metta': '!(probe)\n'}
        for name, text in contents.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        contract = {'reference_commit': 'e' * 40,
                    'reference_files': {p: hashlib.sha256(s.encode()).hexdigest()
                                        for p, s in contents.items()}}
        identity = checker.check_reference(root, contract)
        require(identity['source_kind'] == 'snapshot' and
                identity['checkout_commit'] is None and
                identity['reference_commit'] == contract['reference_commit'] and
                identity['files'] == 5 and identity['examples'] == 1,
                'reference snapshot inherited enclosing Git identity')
        report_pass('reference snapshot identity excludes its enclosing repository')
        with patch.object(checker.subprocess, 'run', return_value=
                          subprocess.CompletedProcess([], 0, str(root), '')), \
             patch.object(checker.subprocess, 'check_output', return_value='0' * 40):
            try:
                checker.check_reference(root, contract)
            except ValueError:
                report_pass('a checkout cannot use snapshot admission to hide a wrong commit')
            else:
                raise AssertionError('reference accepted an unpinned checkout')
        for name, text in [('src/eval.pl', 'eval(wrong).\n'),
                           ('lib/injected.metta', '(Injected)\n')]:
            path = root / name
            path.write_text(text)
            try:
                checker.check_reference(root, contract)
            except ValueError:
                report_pass('reference rejects altered or added source: ' + name)
            else:
                raise AssertionError('reference accepted altered or added source')
            if name in contents:
                path.write_text(contents[name])
        # The additional source belongs to this temporary negative fixture.
        # A successful checker process is still required at readiness entry.
        with patch('cetta_main_readiness.subprocess.run', return_value=
                   subprocess.CompletedProcess([], 1, '', 'wrong reference')):
            try:
                petta_oracle_identity(ROOT, root)
            except RuntimeError:
                report_pass('readiness rejects a failed oracle validation')
            else:
                raise AssertionError('readiness ignored oracle validation failure')
        actual_contract = json.loads((ROOT / 'langdef/petta/reference.json').read_text())
        qualified = dict(identity, reference_commit=actual_contract['reference_commit'],
                         files=len(actual_contract['reference_files']),
                         examples=actual_contract['example_count'],
                         source_manifest_sha256=hashlib.sha256(json.dumps(
                             actual_contract['reference_files'], sort_keys=True,
                             separators=(',', ':')).encode()).hexdigest())
        with patch('cetta_main_readiness.subprocess.run', return_value=
                   subprocess.CompletedProcess([], 0, json.dumps(qualified), '')):
            require(petta_oracle_identity(ROOT, root) == qualified,
                    'readiness replaced the validated oracle identity')
        bad = dict(qualified, reference_commit='0' * 40)
        with patch('cetta_main_readiness.subprocess.run', return_value=
                   subprocess.CompletedProcess([], 0, json.dumps(bad), '')):
            try:
                petta_oracle_identity(ROOT, root)
            except RuntimeError:
                report_pass('readiness rejects a mismatched oracle receipt')
            else:
                raise AssertionError('readiness accepted the wrong oracle commit')
        with patch('cetta_main_readiness.calibration_subject', return_value={'source': 'fixed'}):
            before = {'tier': 'exhaustive', 'petta_oracle': qualified}
            after = dict(before, petta_oracle=dict(qualified, source_manifest_sha256='0' * 64))
            require(source_identity(before) != source_identity(after),
                    'oracle source drift disappeared from tier identity')
        report_pass('tier source identity retains the validated oracle manifest')


def main() -> int:
    global PASS_COUNT
    PASS_COUNT = 0
    check_petta_reference_identity()
    manifest = load_property_manifest(MANIFEST)
    anchors = resolve_source_anchors(ROOT, manifest)
    require(anchors, "source anchors did not resolve")
    require(
        {anchor["property"] for anchor in anchors}
        == {prop["id"] for prop in manifest["properties"]},
        "not every property has a resolved source anchor",
    )
    report_pass(f"readiness manifest {len(manifest['properties'])} properties")
    report_pass(f"readiness source anchors {len(anchors)} resolved")
    reference = json.loads((ROOT / 'langdef/petta/reference.json').read_text())
    upstream = next(name for name in reference['reference_files'] if WORD.search(name))
    ledger = Path('tests/petta/corrected_stable_observations.json')
    field = json.dumps('source') + ': ' + json.dumps(upstream) + ','
    require(pinned_reference_field(ledger, field), 'pinned external identity was rejected')
    for path, changed in (
        (Path('docs/note.txt'), field),
        (ledger, field.replace('"source"', '"assessment"')),
        (ledger, field.replace('import_error', 'another_name')),
        (ledger, field + ' ' + WORD.pattern),
    ):
        require(not pinned_reference_field(path, changed),
                'external identity exemption accepted authored vocabulary')
    report_pass('pinned external identity exemption rejects changed fields, paths and prose')
    require(
        manifest["negative_calibration_witnesses"],
        "negative calibration registry is empty",
    )
    report_pass(
        "readiness negative registry "
        f"{len(manifest['negative_calibration_witnesses'])} resolved"
    )

    check_session_cleanup(
        ["bash", "-c", "(sleep 300) & wait"], leader_exits=False
    )
    report_pass("live readiness process session is cleaned recursively")
    check_session_cleanup(
        ["bash", "-c", "sleep 300 & exit 0"], leader_exits=True
    )
    report_pass("exited readiness session leader cannot strand descendants")
    check_nested_session_cleanup()
    report_pass("nested readiness process sessions are cleaned recursively")
    check_scale_frontier_exit_contract()

    candidate_content = working_tree_content_identity(ROOT)
    require(
        set(candidate_content)
        == {
            "schema",
            "file_count",
            "paths_sha256",
            "content_sha256",
            "excluded_paths",
            "excluded_suffixes",
        },
        "candidate content identity leaked Git topology",
    )
    require(
        candidate_content["excluded_paths"] == ["README.md"],
        "the concurrent README edit was not isolated from readiness identity",
    )
    report_pass("candidate identity is content-addressed, not commit-addressed")

    sizes = [10000.0, 30000.0, 100000.0, 300000.0]
    linear = [(size, 0.00002 * size + 0.2) for size in sizes]
    linear_rss = [(size, 16000.0 + 0.04 * size) for size in sizes]
    require(growth_verdict(linear, linear_rss)["passed"],
            "positive linear calibration was rejected")
    report_pass("positive linear growth calibration")

    quadratic = [(size, 1e-8 * size * size) for size in sizes]
    require(not growth_verdict(quadratic, linear_rss)["passed"],
            "quadratic-growth mutant survived")
    report_pass("mutant quadratic-growth rejected")

    baseline_bytes = [(size, 24.0 * size + 4096.0) for size in sizes]
    require(
        memory_rate_verdict(
            baseline_bytes, max_bytes_per_entry=48.0
        )["passed"],
        "positive bytes-per-entry calibration was rejected",
    )
    leaking_bytes = [(size, 96.0 * size + 4096.0) for size in sizes]
    require(
        not memory_rate_verdict(
            leaking_bytes, max_bytes_per_entry=48.0
        )["passed"],
        "per-entry-leak mutant survived",
    )
    report_pass("mutant per-entry-leak rejected")

    paired_arguments = {
        "max_time_ratio": 1.5,
        "time_absolute_slack": 0.01,
        "max_time_increment_ratio": 1.75,
        "max_time_exponent_delta": 0.35,
        "max_rss_ratio": 1.25,
        "rss_absolute_slack": 4096.0,
        "max_rss_slope_delta": 0.25,
    }
    baseline_time = [(size, 1e-7 * size**1.8) for size in sizes]
    candidate_time = [(size, value * 1.05) for size, value in baseline_time]
    baseline_rss_growth = [(size, 1000.0 + size**0.8) for size in sizes]
    candidate_rss_growth = [
        (size, value * 1.05) for size, value in baseline_rss_growth
    ]
    require(
        paired_growth_verdict(
            candidate_time,
            baseline_time,
            candidate_rss_growth,
            baseline_rss_growth,
            **paired_arguments,
        )["passed"],
        "baseline-equivalent superlinear implementation was rejected",
    )
    report_pass("paired growth accepts unchanged implementation shape")

    fixed_overhead_baseline = [
        (size, 30.0 + 2e-5 * size) for size in sizes
    ]
    lower_cost_candidate = [
        (size, 0.2 + 1e-5 * size) for size in sizes
    ]
    require(
        paired_growth_verdict(
            lower_cost_candidate,
            fixed_overhead_baseline,
            candidate_rss_growth,
            baseline_rss_growth,
            **paired_arguments,
        )["passed"],
        "fixed baseline startup cost caused a false scaling rejection",
    )
    report_pass("paired growth separates startup cost from marginal work")

    noisy_baseline = list(zip(sizes, (2.0, 1.0, 5.0, 9.0)))
    bounded_candidate = list(zip(sizes, (2.0, 2.5, 6.0, 10.0)))
    noisy_result = paired_growth_verdict(
        bounded_candidate,
        noisy_baseline,
        candidate_rss_growth,
        baseline_rss_growth,
        **paired_arguments,
    )
    require(
        noisy_result["time_increments"][0]["limit"]
        == paired_arguments["time_absolute_slack"],
        "negative baseline delta produced a negative increment allowance",
    )
    report_pass("noisy negative baseline increment uses nonnegative work")

    quadratic_candidate = [(size, 1e-10 * size**2) for size in sizes]
    linear_baseline = [(size, 2e-5 * size) for size in sizes]
    quadratic_result = paired_growth_verdict(
        quadratic_candidate,
        linear_baseline,
        candidate_rss_growth,
        baseline_rss_growth,
        **paired_arguments,
    )
    require(
        all(cell["passed"] for cell in quadratic_result["time_cells"]),
        "quadratic canary did not isolate the marginal-time contract",
    )
    require(
        not quadratic_result["time_increment_passed"]
        and not quadratic_result["passed"],
        "paired quadratic-growth mutant survived",
    )
    report_pass("paired growth rejects a complexity-class regression")

    tuned_superlinear = paired_growth_verdict(
        list(zip(sizes, (0.040, 0.182, 0.971, 4.471))),
        list(zip(sizes, (0.100, 0.300, 1.000, 3.000))),
        candidate_rss_growth,
        baseline_rss_growth,
        **paired_arguments,
    )
    require(
        all(cell["passed"] for cell in tuned_superlinear["time_cells"])
        and tuned_superlinear["time_increment_passed"],
        "tuned-superlinear canary did not isolate exponent assurance",
    )
    require(
        not tuned_superlinear["time_exponent_passed"]
        and not tuned_superlinear["passed"],
        "lower-constant superlinear mutant survived marginal-rate analysis",
    )
    report_pass("marginal rates reject a tuned lower-constant exponent regression")

    require(
        not paired_growth_verdict(
            [(size, value * 2.0) for size, value in baseline_time],
            baseline_time,
            candidate_rss_growth,
            baseline_rss_growth,
            **paired_arguments,
        )["passed"],
        "paired constant-time slowdown survived",
    )
    report_pass("paired growth rejects a material constant slowdown")

    require(
        not paired_growth_verdict(
            candidate_time,
            baseline_time,
            [(size, value * 2.0) for size, value in baseline_rss_growth],
            baseline_rss_growth,
            **paired_arguments,
        )["passed"],
        "paired RSS-growth mutant survived",
    )
    report_pass("paired growth rejects material RSS growth")

    route_ok, _ = route_contract("pathmap-primary", "native-space")
    require(not route_ok, "wrong-backend-routing mutant survived")
    report_pass("mutant wrong-backend-routing rejected")

    observer_counters = {
        "cost-rho-parallel-state-only-run": 1,
        "cost-rho-receipt-event-retained": 1,
    }
    observer_ok, _ = counter_contract(
        observer_counters,
        positive=("cost-rho-parallel-state-only-run",),
        zero=("cost-rho-receipt-event-retained",),
    )
    require(not observer_ok, "observer-work-state-only mutant survived")
    report_pass("mutant observer-work-state-only rejected")

    claim_counters = {"acquired": 7, "committed": 3, "released": 3}
    claim_ok, _ = counter_contract(
        claim_counters,
        conservation=(("acquired", "committed", "released"),),
    )
    require(not claim_ok, "broken-claim-rollback mutant survived")
    report_pass("mutant broken-claim-rollback rejected")

    identity = {
        "schema": "cetta.main-readiness.evidence.v3",
        "candidate": "source-a",
        "binary": "binary-a",
        "inputs": "inputs-a",
        "compiler": "gcc-a",
        "dependencies": {"mork": "mork-a"},
        "suite": "suite-a",
    }
    original = evidence_key(identity)
    for field in ("candidate", "binary", "inputs", "compiler", "suite"):
        changed = dict(identity)
        changed[field] = f"{field}-b"
        require(evidence_key(changed) != original,
                f"evidence key ignored {field}")
    changed_dependencies = json.loads(json.dumps(identity))
    changed_dependencies["dependencies"]["mork"] = "mork-b"
    require(evidence_key(changed_dependencies) != original,
            "evidence key ignored dependency commit")
    report_pass("content-addressed evidence key covers all required dimensions")

    artifact = {
        "schema": "cetta.main-readiness.evidence.v3",
        "tier": "routine",
        "candidate": {"tree": "candidate-a", "diff": "diff-a"},
        "binaries": {"cetta": "binary-a"},
        "inputs": {"suite": "inputs-a"},
        "compiler_and_build": {
            "generated": {"config.h": "generated-a"},
            "gcc": "gcc-a",
            "make": "make-a",
            "python": "python-a",
        },
        "dependencies": {"MORK": {"tree": "mork-a"}},
        "property_schema": "properties-v1",
        "property_manifest_sha256": "manifest-a",
        "source_anchors": [{"path": "source.c", "sha256": "source-a"}],
        "consolidation_baseline": {"commit": "c632", "binary_sha256": "binary-c"},
    }
    subject = calibration_subject(artifact)
    changed_incidentals = json.loads(json.dumps(artifact))
    changed_incidentals["tier"] = "exhaustive"
    changed_incidentals["binaries"]["cetta"] = "binary-b"
    changed_incidentals["compiler_and_build"]["generated"]["config.h"] = (
        "generated-b"
    )
    require(
        calibration_subject(changed_incidentals) == subject,
        "calibration subject included tier-local build products",
    )
    report_pass("calibration subject excludes tier-local build products")
    changed_baseline = json.loads(json.dumps(artifact))
    changed_baseline["consolidation_baseline"]["binary_sha256"] = "wrong-binary"
    require(calibration_subject(changed_baseline) != subject,
            "calibration reused evidence from another consolidation baseline")
    report_pass("calibration subject pins consolidation baseline")

    subject_key = evidence_key(subject)
    records = [
        {
            "pair_id": f"pair-{index}",
            "subject_key": subject_key,
            "routine_status": "passed",
            "exhaustive_status": "passed",
            "routine_mutants_killed": True,
            "baseline_match": True,
            "common_prefix_match": True,
        }
        for index in range(5)
    ]
    require(
        qualification_verdict(
            records, required_runs=5, subject_key=subject_key
        )["qualified"],
        "five clean differential pairs did not qualify",
    )
    report_pass("five clean differential pairs qualify")

    disagreement = json.loads(json.dumps(records))
    disagreement[-1]["exhaustive_status"] = "failed"
    require(
        not qualification_verdict(
            disagreement, required_runs=5, subject_key=subject_key
        )["qualified"],
        "routine/exhaustive disagreement was accepted",
    )
    report_pass("routine/exhaustive disagreement blocks qualification")

    unqualified_mutant = json.loads(json.dumps(records))
    unqualified_mutant[-1]["routine_mutants_killed"] = False
    require(
        not qualification_verdict(
            unqualified_mutant, required_runs=5, subject_key=subject_key
        )["qualified"],
        "surviving mutant was accepted",
    )
    report_pass("surviving mutant blocks qualification")

    unauthenticated_prefix = json.loads(json.dumps(records))
    unauthenticated_prefix[-1]["common_prefix_match"] = False
    require(
        not qualification_verdict(
            unauthenticated_prefix,
            required_runs=5,
            subject_key=subject_key,
        )["qualified"],
        "unauthenticated common-prefix evidence was accepted",
    )
    report_pass("unauthenticated common-prefix reuse blocks qualification")

    require(
        census_classification({"passed": True, "status": "completed"})
        == ("progress", True),
        "completed D4 census was not classified as observed progress",
    )
    require(
        census_classification(
            {"passed": False, "status": "bounded-progress"}
        )
        == ("boundary", True),
        "bounded D4 census was incorrectly granted readiness credit",
    )
    require(
        census_classification({"passed": False, "status": "failed"})
        == ("failed", False),
        "failed D4 process was accepted as a capability boundary",
    )
    report_pass("D4 capability census remains non-authoritative")

    malformed = json.loads(MANIFEST.read_text(encoding="utf-8"))
    malformed["properties"][1]["largest_scale_class"] = (
        malformed["properties"][0]["largest_scale_class"]
    )
    scratch = ROOT / "runtime/main-readiness-manifest-negative.json"
    scratch.parent.mkdir(parents=True, exist_ok=True)
    scratch.write_text(json.dumps(malformed), encoding="utf-8")
    try:
        try:
            load_property_manifest(scratch)
        except ReadinessModelError:
            pass
        else:
            raise AssertionError("duplicate largest-scale class was accepted")
    finally:
        scratch.unlink(missing_ok=True)
    report_pass("duplicate largest-scale class rejected")

    malformed = json.loads(MANIFEST.read_text(encoding="utf-8"))
    malformed["properties"][0]["largest_scale_witness"] = (
        malformed["properties"][0]["routine_witnesses"][0]
    )
    scratch.write_text(json.dumps(malformed), encoding="utf-8")
    try:
        try:
            load_property_manifest(scratch)
        except ReadinessModelError:
            pass
        else:
            raise AssertionError("routine proxy was accepted as largest-scale witness")
    finally:
        scratch.unlink(missing_ok=True)
    report_pass("largest-scale witness must belong to exhaustive evidence")

    manifest = load_property_manifest(MANIFEST)
    largest_size = manifest["exhaustive_ladder_sizes"][-1]
    executed = [
        {"kind": "backend", "case": case, "size": largest_size}
        for case in (
            "native", "pathmap", "mork-live", "mork-open-act", "mork-load-act"
        )
    ] + [
        {"kind": "transfer", "case": "native-to-pathmap", "size": largest_size}
    ]
    produced = set(realized_witness_ids("exhaustive", executed))
    required = required_ladder_witness_ids(manifest)
    require(
        required <= produced,
        "executed largest-scale rows did not produce their manifest witnesses",
    )
    produced_without_native = set(
        realized_witness_ids(
            "exhaustive",
            [row for row in executed if row["case"] != "native"],
        )
    )
    require(
        f"space-ladder-exhaustive:native@{largest_size}"
        in required - produced_without_native,
        "missing executed largest-scale row remained self-certified",
    )
    report_pass("largest-scale witness identities are execution-derived")

    transfer_row = {
        "case_id": "native-to-pathmap",
        "fact_count": "100",
        "count": "100",
        "term_universe_inserts": "200",
        "term_universe_blob_bytes": "4096",
        "space_atom_id_capacity_bytes_peak": "1024",
        "pathmap_direct_store": "100",
        "mork_add_call": "0",
        "mork_add_batch_items": "0",
    }
    require(
        not primary_contract("transfer", transfer_row),
        "positive transfer counter contract failed",
    )
    transfer_mutant = dict(transfer_row)
    transfer_mutant["pathmap_direct_store"] = "0"
    require(
        bool(primary_contract("transfer", transfer_mutant)),
        "transfer counter mutant survived",
    )
    report_pass("transfer runtime counter mutation rejected")

    routine_targets = {command[-1] for command in readiness_commands("routine")}
    exhaustive_targets = {
        command[-1] for command in readiness_commands("exhaustive")
    }
    settled_petta_targets = {
        "test-petta-release-reference", "test-petta-corrected-stable",
        "test-petta-swi-differences", "test-language-versions",
        "test-petta-language-versions", "test-petta-versioned-imports",
    }
    require(settled_petta_targets <= routine_targets & exhaustive_targets,
            "readiness omits a settled PeTTa contract gate")
    for tier in ("routine", "exhaustive"):
        targets = [command[-1] for command in readiness_commands(tier)]
        require(all(targets.index(target) < targets.index("test-correctness-all")
                    for target in settled_petta_targets),
                "broad readiness starts before validating its PeTTa contract")
    report_pass("both tiers qualify corrected-stable PeTTa before the broad matrix")
    require("main-readiness-consolidation" in routine_targets & exhaustive_targets,
            "a readiness tier omits the original consolidation baseline")
    require("bench-evaluator-tax" in routine_targets & exhaustive_targets,
            "a readiness tier checks evaluator routes without their instruction budgets")
    require("main-readiness-rho-adaptive" in routine_targets and "bench-heavy" in exhaustive_targets,
            "consolidation replaced a historical regression gate")
    report_pass("both tiers retain historical gates and require consolidation timing")
    petta_oracle_targets = {
        "test-petta-corpus-differential",
        "test-petta-native-core-no-libpl",
    }
    require(
        petta_oracle_targets.isdisjoint(routine_targets),
        "external PeTTa oracle differentials burden the routine developer tier",
    )
    require(
        petta_oracle_targets <= exhaustive_targets,
        "exhaustive readiness omits a PeTTa oracle differential",
    )
    report_pass("PeTTa oracle differentials are exhaustive-only merge gates")

    routine_ladder = "main-readiness-space-ladders"
    stress_ladder = "main-readiness-space-ladders-exhaustive"
    frontier_ladder = "main-readiness-space-frontier"
    exhaustive_order = [
        command[-1] for command in readiness_commands("exhaustive")
    ]
    require(
        routine_ladder in routine_targets
        and stress_ladder not in routine_targets
        and frontier_ladder not in routine_targets,
        "routine readiness does not isolate the paired ladder from 1M stress",
    )
    require(
        routine_ladder in exhaustive_targets
        and stress_ladder in exhaustive_targets
        and frontier_ladder in exhaustive_targets,
        "exhaustive readiness must run paired regression, candidate stress, and frontier",
    )
    require(
        exhaustive_order.index(routine_ladder)
        < exhaustive_order.index(stress_ladder)
        < exhaustive_order.index(frontier_ladder),
        "storage stress/frontier order is not progressively increasing",
    )
    report_pass("exhaustive storage checks progress from paired to 1M to 10M")

    schedule_fixture = [
        ("backend", "native", size) for size in (1, 2, 4, 8)
    ] + [
        ("transfer", "native-to-pathmap", size) for size in (1, 2, 4, 8)
    ]
    schedules = [scheduled_cases(schedule_fixture, index) for index in range(3)]
    require(
        all(sorted(schedule) == sorted(schedule_fixture) for schedule in schedules),
        "a repeated ladder schedule omitted or duplicated a cell",
    )
    require(
        len({tuple(schedule) for schedule in schedules}) == len(schedules),
        "repeated ladder schedules preserve a confounded monotone order",
    )
    report_pass("repeated ladder samples use complete distinct schedules")

    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    heavy_body = makefile.split("\nbench-heavy:\n", 1)[1].split(
        "\nbench-prime-light:", 1
    )[0]
    require(
        "bench-space-scale-ladder" not in heavy_body,
        "10M frontier was duplicated inside mandatory bench-heavy",
    )
    require(
        "probe-d4-nodup-capability-backends" not in heavy_body,
        "duplicate non-authoritative D4 census returned to bench-heavy",
    )
    require(
        "main-readiness-rho-adaptive" in heavy_body,
        "bench-heavy lost adaptive paired rho qualification",
    )
    require(
        "\nbench-space-scale-ladder:\n" in makefile,
        "explicit standalone 10M frontier target is missing",
    )
    require(
        "\nmain-readiness-space-frontier:\n" in makefile,
        "exhaustive readiness has no witnessed 10M frontier target",
    )
    adaptive_body = makefile.split("\nmain-readiness-rho-adaptive:\n", 1)[1].split(
        "\nmain-readiness-routine:", 1
    )[0]
    require(
        "if ! out=$$(./scripts/compare_witness.sh --enforce-rss-against"
        in adaptive_body
        and "printf '%s\\n' \"$$out\"; exit 1" in adaptive_body,
        "adaptive rho RSS enforcement can swallow a failing comparison",
    )
    report_pass("mandatory heavy graph does not duplicate the explicit frontier")
    check_frozen_baseline()
    check_consolidation_matrix()
    print(f"SUMMARY cetta-readiness-model {PASS_COUNT} PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, ReadinessModelError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
