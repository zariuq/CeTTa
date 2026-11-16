#!/usr/bin/env python3
"""Native observer controls at the value writer and actual CLI boundaries."""
import argparse
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile

import native_observation as observed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("value_writer", type=Path)
    parser.add_argument("--artifacts", type=Path, default=Path("runtime"))
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    with tempfile.TemporaryDirectory(prefix="native-observation-", dir=args.artifacts.resolve()) as directory:
        work = Path(directory)
        values, opaque = work / "values.jsonl", work / "opaque.jsonl"
        subprocess.run([str(args.value_writer.resolve()), str(values), str(opaque)], check=True, timeout=30)
        document = observed.read_observation(values)
        answers = document["queries"][1]["answers"]
        assert len(answers) == 13
        assert answers[0] == [["integer", "1"]]
        assert answers[1] == [["float64", "3ff0000000000000"]]
        assert answers[2] != answers[3]
        assert answers[4] == [["boolean", True]] and answers[5] == [["symbol", "54727565"]]
        assert answers[6] == [["string", "6100ff62"]]
        assert answers[7] == [["expression", 0]]
        assert answers[8] != answers[9], "nullary list and compound must stay distinct"
        assert answers[10] == [["expression", 3], ["variable", 0], ["variable", 1], ["variable", 0]]
        assert len(answers[11]) == 258 and answers[11][1] == answers[11][-1]
        assert answers[11][-2] == ["variable", 257]
        assert len(answers[12]) == 10001
        assert document["queries"][2]["answers"] == [[["variable", 0]]]
        try:
            observed.read_observation(opaque)
            raise AssertionError("an unknown grounded value qualified by its kind alone")
        except observed.UnobservedValue:
            pass

        source = work / "program.metta"
        source.write_text('!(let $_ (println! @@START) first-value)\n'
                          '!(let $_ (println! left) (let $_ (println! right) second-value))\n'
                          '!(superpose (duplicate duplicate))\n'
                          '!(superpose ())\n')
        events = work / "events.jsonl"
        baseline = subprocess.run([binary, "--lang", "petta", str(source)], capture_output=True, timeout=30)
        result = subprocess.run([binary, "--lang", "petta", "--run-observation", str(events), str(source)],
                                capture_output=True, timeout=30)
        assert baseline.returncode == result.returncode == 0
        assert baseline.stdout == result.stdout
        run = observed.read_observation(events)
        view = observed.project(run, [(i, i) for i in range(1, 5)])
        assert view[0]["effects"] == [(0, b"@@START\n")]
        assert view[1]["effects"] == [(0, b"left\nright\n")]
        assert len(view[2]["answers"]) == 2 and view[2]["answers"][0] == view[2]["answers"][1]
        assert view[3]["answers"] == []

        # CLI-looking guest values must not activate the guard or select a
        # report destination. The actual option parser owns this boundary.
        source.write_text('!(import! &self (library system))\n!(system:args)\n')
        untouched = work / "guest-destination"
        untouched.write_bytes(b"keep this data")
        guest_args = ["--run-report", str(untouched), "--run-observation", "guest-value", "--run-contract"]
        baseline = subprocess.run([binary, "--lang", "petta", str(source), *guest_args],
                                  capture_output=True, timeout=30)
        result = subprocess.run([binary, "--lang", "petta", "--run-observation", str(events),
                                 str(source), *guest_args], capture_output=True, timeout=30)
        assert baseline.returncode == result.returncode == 0
        assert baseline.stdout == result.stdout and untouched.read_bytes() == b"keep this data"
        assert observed.read_observation(events)["final"]["contract_exit"] == 0

        # A valid empty frontier is different from an interrupted frontier.
        source.write_text('(= (loop) (loop))\n!(loop)\n!(+ 40 2)\n')
        pending = subprocess.run([binary, "--lang", "petta", "--fuel", "50", "--run-observation",
                                  str(events), str(source)], capture_output=True, timeout=30)
        assert pending.returncode != 0
        run = observed.read_observation(events)
        assert run["final"]["contract_exit"] != 0 and run["final"]["queries_unsettled"] > 0
        try:
            observed.project(run, [(1, 1)])
            raise AssertionError("an interrupted frontier qualified as empty")
        except ValueError:
            pass

        # Missing, duplicated, truncated and post-terminal receipts cannot
        # manufacture a complete observation, even when answers agree.
        records = [json.loads(line) for line in values.read_text().splitlines()]
        variants = [records[:-1], records + [records[-1]], copy.deepcopy(records)]
        variants[2][-1]["queries_declared"] += 1
        for i, broken in enumerate(variants):
            path = work / f"broken-{i}.jsonl"
            path.write_text("".join(json.dumps(event) + "\n" for event in broken))
            try:
                observed.read_observation(path)
                raise AssertionError("a broken observation receipt qualified")
            except ValueError:
                pass
        if Path("/dev/full").exists():
            source.write_text('!(+ 40 2)\n')
            failed = subprocess.run([binary, "--lang", "petta", "--run-observation", "/dev/full", str(source)],
                                    capture_output=True, timeout=30)
            assert failed.returncode != 0
        source.write_text('!(+ 40 2)\n')
        closed = subprocess.run([binary, "--lang", "petta", "--run-observation", str(events), str(source)],
                                capture_output=True, timeout=30, preexec_fn=lambda: os.close(1))
        assert closed.returncode != 0, "the observer must not resurrect closed standard output"
    print("PASS: native answer kinds, aliasing, duplicates, empty/pending distinction, separate effects and receipt failures")


if __name__ == "__main__":
    main()
