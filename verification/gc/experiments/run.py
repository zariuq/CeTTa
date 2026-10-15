#!/usr/bin/env python3
"""Matched symbol experiments, with independent future-use/negative controls."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--prototype", required=True, type=Path)
parser.add_argument("--owners", required=True, type=Path)
parser.add_argument("--engine", type=Path)
parser.add_argument("--reference", type=Path)
parser.add_argument("--omega-harness", type=Path)
parser.add_argument("--turns", type=int, default=32768)
parser.add_argument("--output", type=Path, default=Path("runtime/gc-experiments/results"))
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
for key in list(env):
    if key.startswith("CETTA_GC_") or key.startswith("HARNESS_"): env.pop(key)


def run(name, command, overrides=None, expected=0):
    settings = env | (overrides or {})
    result = subprocess.run(command, env=settings, capture_output=True, text=True, timeout=90)
    (args.output / (name + ".stdout")).write_text(result.stdout)
    (args.output / (name + ".stderr")).write_text(result.stderr)
    if result.returncode != expected:
        raise RuntimeError(f"{name}: exit {result.returncode}, expected {expected}: {result.stderr[-500:]}")
    return result


def record(prefix, text):
    matches = [json.loads(line[len(prefix):]) for line in text.splitlines() if line.startswith(prefix)]
    if not matches: raise RuntimeError("missing diagnostic: " + prefix)
    return matches[-1]


results = {"scope": "isolated prototypes and selected real owners; not production GC qualification",
           "prototype_sha256": hashlib.sha256(args.prototype.read_bytes()).hexdigest(),
           "owners_sha256": hashlib.sha256(args.owners.read_bytes()).hexdigest()}
def save_results():
    (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
proto = str(args.prototype.resolve())
churn = run("churn", [proto, str(args.turns)])
results["churn"] = [json.loads(line) for line in churn.stdout.splitlines()]
for mode in ["weak-intern", "managed-spelling-key-gc"]:
    for keyed in [0, 64]:
        rows = [row for row in results["churn"] if row["mode"] == mode and row["keyed_every"] == keyed]
        assert len(rows) == 4 and rows[-1]["payload_bytes"] == 0
        assert rows[0]["payload_bytes"] == rows[-2]["payload_bytes"]
weak = [row for row in results["churn"] if row["mode"] == "weak-intern" and row["keyed_every"] == 0]
assert weak[-2]["metadata_bytes"] > weak[0]["metadata_bytes"], "run must expose retired-ID metadata growth"
uncollected = [row for row in results["churn"] if row["mode"] == "managed-spelling" and row["keyed_every"] == 64]
assert uncollected[-1]["owned_payload_bytes"] > uncollected[0]["owned_payload_bytes"]
results["mutants"] = []
for mutant in ["omit-root", "bad-hash", "reuse-id"]:
    result = run(mutant, [proto, mutant], expected=1)
    assert "detected-mutant:" in result.stdout
    results["mutants"].append({"name": mutant, "detected": True})

results["owners"] = []
owner_binary = str(args.owners.resolve())
for family in ["syntax", "spaces", "state", "closures", "bindings", "term-graphs",
               "delays", "foreign-leases", "durable", "prepared", "named-state"]:
    for omitted in [False, True]:
        setting = {"CETTA_GC_SHADOW_EVERY": "1"}
        if omitted: setting["CETTA_GC_SHADOW_OMIT"] = family
        result = run(f"owner-{family}-{omitted}", [owner_binary, family], setting,
                     expected=1 if omitted else 0)
        assert ("detected omitted visitor" if omitted else "future read covered") in result.stdout
        results["owners"].append({"owner": family, "omission_control": omitted, "passed": True})

if args.omega_harness:
    if not args.engine or not args.reference: parser.error("Omega trials require --engine and --reference")
    results["engine_sha256"] = hashlib.sha256(args.engine.read_bytes()).hexdigest()
    results["reference_sha256"] = hashlib.sha256(args.reference.read_bytes()).hexdigest()
    results["omega_core_sha256"] = hashlib.sha256(
        (args.omega_harness / "repos/Omega/src/loop.metta").read_bytes()).hexdigest()
    results["omega"] = []
    for case, steps, active, every, wrapper, shadow in [
        ("idle-100", 100, 2, 0, "run.metta", 256),
        ("idle-400", 400, 2, 0, "run.metta", 256),
        ("lazy-400", 400, 2, 0, "run-lazy.metta", 256),
        ("busy-30", 30, 50, 3, "run.metta", 1),
        ("mixed-30", 30, 2, 7, "run.metta", 1),
    ]:
        settings = {"PYTHONPATH": str(args.omega_harness.resolve()), "HARNESS_STEPS": str(steps),
                    "HARNESS_ACTIVE_LOOPS": str(active), "HARNESS_MESSAGE_EVERY": str(every)}
        summaries = []
        for variant, binary in [("reference", args.reference), ("probe", args.engine)]:
            config = dict(settings)
            if variant == "probe": config |= {"CETTA_GC_PROBE_DUMP": "1", "CETTA_GC_SHADOW_EVERY": str(shadow)}
            result = run(case + "-" + variant, [str(binary.resolve()), "--profile", "extended",
                         "--lang", "petta", str((args.omega_harness / wrapper).resolve())], config)
            summary = record("harness-summary: ", result.stdout)
            summaries.append(summary)
            if variant == "probe":
                row = {"case": case, "summary": summary,
                       "births": record("gc-key-probe: ", result.stderr),
                       "shadow": record("gc-shadow-probe: ", result.stderr)}
                results["omega"].append(row)
                save_results()
                assert row["shadow"]["choice_scans"] > 0
                row["scanner_refuted"] = row["shadow"]["missed_reads"] != 0
                assert row["shadow"]["outdated_snapshots"] == 0
        assert summaries[0] == summaries[1], case + ": instrumentation changed observable trace"
        results["omega"][-1]["observable_trace_matches"] = True

    # A short run missed this real owner. The longer trial must detect its
    # deliberate omission without changing the guest's observable trace.
    idle = next(row for row in results["omega"] if row["case"] == "idle-400")
    result = run("idle-400-omit-named-state", [str(args.engine.resolve()), "--profile",
                 "extended", "--lang", "petta",
                 str((args.omega_harness / "run.metta").resolve())],
                 {"PYTHONPATH": str(args.omega_harness.resolve()), "HARNESS_STEPS": "400",
                  "HARNESS_ACTIVE_LOOPS": "2", "HARNESS_MESSAGE_EVERY": "0",
                  "CETTA_GC_PROBE_DUMP": "1", "CETTA_GC_SHADOW_EVERY": "256",
                  "CETTA_GC_SHADOW_OMIT": "named-state"})
    omitted = record("gc-shadow-probe: ", result.stderr)
    assert omitted["missed_reads"] > 0, "missing named-state visitor escaped the runtime oracle"
    assert record("harness-summary: ", result.stdout) == idle["summary"]
    results["omega_omission_control"] = {
        "case": "idle-400-omit-named-state", "detected": True,
        "shadow": omitted, "observable_trace_matches": True}

save_results()
print("Symbol experiments completed; results:", args.output / "results.json")
if any(row.get("scanner_refuted") for row in results.get("omega", [])):
    print("Partial runtime scanner REFUTED: retain the missing-owner evidence; freeing remains disabled.")
