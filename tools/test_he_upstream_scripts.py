#!/usr/bin/env python3
"""Compare upstream script observations case by case, retaining baseline gaps."""

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from metta_result_observation import output_observation


def identity(path):
    return {"path": str(path), "sha256": sha256(path.read_bytes()).hexdigest()}


def run(command, directory, environment):
    process = subprocess.Popen(command, cwd=directory, env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate()
        return {"exit_status": None, "stdout": "", "stderr": "",
                "diagnostic": "timeout; no language result inferred"}
    return {"exit_status": process.returncode, "stdout": stdout, "stderr": stderr}


def observation(output):
    text = re.sub(r"GroundingSpace-0x[0-9a-fA-F]+|<space 0x[0-9a-fA-F]+>", "SPACE", output)
    try:
        return output_observation(text)
    except ValueError:
        return [("unreadable", text)]


def reference_worker(argument):
    import hyperon
    import hyperonpy
    from hyperon import Environment, MeTTa

    if argument == "identity":
        print(json.dumps({"python": identity(Path(sys.executable)),
                          "package": identity(Path(hyperon.__file__)),
                          "extension": identity(Path(hyperonpy.__file__))}))
        return
    path = Path(argument).resolve()
    runner = MeTTa(env_builder=Environment.custom_env(working_dir=str(path.parent)))
    for answers in runner.run(path.read_text(encoding="utf-8")):
        print("[" + ", ".join(map(str, answers)) + "]", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--reference-python", type=Path, required=True)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--baseline-receipt", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    upstream = args.upstream.resolve()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONUNBUFFERED="1")
    environment.pop("PYTHONPATH", None)
    reference = [str(args.reference_python.absolute()), str(Path(__file__).resolve()), "--reference"]
    ref_identity = run(reference + ["identity"], work, environment)
    assert ref_identity["exit_status"] == 0, ref_identity
    reference_identity = json.loads(ref_identity["stdout"])
    baseline = json.loads(args.baseline_receipt.read_text()) if args.baseline_receipt else None
    if baseline:
        assert baseline["reference_identity"] == reference_identity, "Reference changed since baseline"
    cases = sorted((upstream / "python/tests/scripts").glob("*.metta"))
    cases.append(upstream / "lib/tests/test_stdlib.metta")
    assert len(cases) == 27, ("Upstream case inventory changed", len(cases))
    records = []
    regressions = []
    for path in cases:
        key = str(path.relative_to(upstream))
        pinned = run(reference + [str(path)], work, environment)
        for profile in ("he-compat", "he", "extended"):
            actual = run([str(binary), "--lang", "he", "--profile", profile, str(path)],
                         path.parent, environment)
            agrees = pinned["exit_status"] == actual["exit_status"] == 0 and (
                observation(pinned["stdout"]) == observation(actual["stdout"]))
            record = {"case": key, "profile": profile, "source_sha256": sha256(path.read_bytes()).hexdigest(),
                      "reference": pinned, "actual": actual, "agrees": agrees}
            if baseline:
                previous = next(row for row in baseline["records"]
                                if row["case"] == key and row["profile"] == profile)
                assert previous["source_sha256"] == record["source_sha256"], "Upstream case changed"
                record["changed"] = observation(previous["actual"]["stdout"]) != observation(actual["stdout"])
                if (previous["agrees"] and not agrees) or actual["exit_status"] != 0:
                    regressions.append({"case": key, "profile": profile})
            records.append(record)
            print(f"{'SAME' if agrees else 'DIFF'} {profile} {key}", flush=True)
    receipt = {"format": "he-upstream-scripts-v1", "binary_identity": identity(binary),
               "reference_identity": reference_identity, "records": records,
               "driver_sha256": sha256(Path(__file__).read_bytes()).hexdigest(),
               "baseline_receipt_sha256": sha256(args.baseline_receipt.read_bytes()).hexdigest()
                                          if args.baseline_receipt else None,
               "regressions": regressions}
    with args.receipt.open("x") as stream:
        json.dump(receipt, stream, indent=2, sort_keys=True)
        stream.write("\n")
    for profile in ("he-compat", "he", "extended"):
        print(profile, "identical on", sum(row["agrees"] for row in records if row["profile"] == profile),
              "of", len(cases))
    if baseline:
        print("Case-by-case regressions:", len(regressions))
    raise SystemExit(bool(regressions))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--reference":
        reference_worker(sys.argv[2])
    else:
        main()
