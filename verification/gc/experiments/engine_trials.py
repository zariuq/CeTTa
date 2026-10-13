#!/usr/bin/env python3
"""Matched actual-engine trials; all generated state lives under --output.

No live providers, credentials or channels are used. The optional Omega
harness must supply synthetic host adapters and the unchanged pinned loop.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OMEGA_LOOP_SHA = "21f7db7ef994b566451468a7e6341b475ab73f3237fb9e65706407cafc4ad2b7"
CHURN_HOST = '''import json
def fact(n):
    return "(churn-fact spelling-%d-%s)" % (int(n), "x" * 4096)
def key(n):
    return "churn-call-%d" % int(n)
def snapshot(items):
    data = {str(r[1]): int(r[2]) for r in items if len(r) == 3 and str(r[0]) == "runtime-counter"}
    keep = {k:v for k,v in data.items() if "arena-" in k or k.startswith(("symbol-", "managed-symbol-", "hashcons-", "term-universe-"))}
    print("churn-stats: " + json.dumps(keep, sort_keys=True), flush=True)
    return True
'''
CHURN_BODY = {
    "temporary": '(let $value (sread (py-call (gc_churn_host.fact $n))) (churn (- $n 1)))',
    "facts": '(let $value (sread (py-call (gc_churn_host.fact $n))) (progn (add-atom &churn-bag $value) (remove-atom &churn-bag $value) (churn (- $n 1))))',
    "keys": '(let $key (sread (py-call (gc_churn_host.key $n))) (progn (add-atom &self (= ($key) $n)) (remove-atom &self (= ($key) $n)) (churn (- $n 1))))',
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def records(prefix, output):
    return [json.loads(line[len(prefix):]) for line in output.splitlines()
            if line.startswith(prefix)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("runtime/gc-experiments/engine-trials"))
    parser.add_argument("--omega-harness", type=Path)
    parser.add_argument("--omega-loop", type=Path,
                        help="Exact loop imported by the synthetic harness")
    args = parser.parse_args()
    if bool(args.omega_harness) != bool(args.omega_loop):
        parser.error("Supply both --omega-harness and --omega-loop")
    if args.omega_loop and sha(args.omega_loop) != OMEGA_LOOP_SHA:
        parser.error("Omega loop differs from the pinned unchanged core")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    engines = {"reference": args.reference.resolve(), "managed": args.engine.resolve()}
    receipt = {"scope": "actual engine; transient-value prototype, not complete GC",
               "binary_sha256": {k: sha(v) for k, v in engines.items()},
               "churn": [], "omega": []}
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("CETTA_GC_", "HARNESS_"))}

    def save():
        (out / "results.json").write_text(json.dumps(receipt, indent=2) + "\n")

    def run(label, variant, source, cwd=ROOT, overrides=None):
        log = out / (label + "-" + variant + ".time")
        result = subprocess.run(
            ["/usr/bin/time", "-v", "-o", str(log), str(engines[variant]),
             "--emit-runtime-stats", "--profile", "extended", "--lang", "petta", str(source)],
            cwd=cwd, env=env | (overrides or {}),
            capture_output=True, text=True, timeout=60)
        (out / (label + "-" + variant + ".stdout")).write_text(result.stdout)
        (out / (label + "-" + variant + ".stderr")).write_text(result.stderr)
        if result.returncode:
            raise RuntimeError(f"{label}/{variant}: exit {result.returncode}: {result.stderr[-500:]}")
        rss = next(int(line.split(":", 1)[1]) for line in log.read_text().splitlines()
                   if "Maximum resident set size (kbytes)" in line)
        return result, rss

    owner = ROOT / "tests/petta/managed_symbol_owners.metta"
    expected = owner.with_suffix(".expected").read_text()
    for variant in engines:
        result, _ = run("owners", variant, owner)
        if result.stdout != expected:
            raise AssertionError(f"{variant}: actual owner observations differ from fixture")
    receipt["owners_passed"] = True

    (out / "gc_churn_host.py").write_text(CHURN_HOST)
    for family, body in CHURN_BODY.items():
        for count in (100, 400):
            source = out / f"churn-{family}-{count}.metta"
            source.write_text(f'(= (churn $n) (if (> $n 0) {body} done))\n!(churn {count})\n'
                              '!(collapse (get-atoms &churn-bag))\n'
                              '!(py-call (gc_churn_host.snapshot (collapse (get-atoms (runtime-stats!)))))\n')
            observations = []
            for variant in engines:
                result, rss = run(source.stem, variant, source,
                                  overrides={"PYTHONPATH": str(out)})
                gauges = records("churn-stats: ", result.stdout)
                if len(gauges) != 1:
                    raise AssertionError("missing runtime storage gauges")
                observations.append([l for l in result.stdout.splitlines()
                                     if not l.startswith("churn-stats: ")])
                row = {"family": family, "steps": count, "engine": variant,
                       "rss_kib": rss, "gauges": gauges[0], "source_sha256": sha(source)}
                receipt["churn"].append(row)
                if variant == "managed" and family == "temporary":
                    if gauges[0].get("managed-symbol-retained-bytes") != 0:
                        raise AssertionError("temporary spelling payload survived its owners")
                save()
            if observations[0] != observations[1]:
                raise AssertionError(f"{family}/{count}: guest observations differ")
            print(f"PASS: {family}, {count} births; observations matched", flush=True)

    temporary = [r["gauges"] for r in receipt["churn"]
                 if r["family"] == "temporary" and r["engine"] == "managed"]
    # A disabled-feature engine also reports zero managed retained bytes.
    # Require real managed allocation and stable table/index storage as well.
    for gauges in temporary:
        if gauges.get("managed-symbol-allocated-bytes", 0) == 0:
            raise AssertionError("the managed representation was not exercised")
    for key in ("symbol-data-bytes", "symbol-owned-bytes", "symbol-metadata-bytes",
                "hashcons-thread-entries", "hashcons-thread-index-bytes"):
        if key not in temporary[0] or temporary[0][key] != temporary[1][key]:
            raise AssertionError("transient growth moved into another index: " + key)

    if args.omega_harness:
        receipt["omega_loop_sha256"] = sha(args.omega_loop)
        for name, count, active, every in (
            ("idle100", 100, 2, 0), ("idle400", 400, 2, 0),
            ("busy", 30, 50, 3), ("mixed", 30, 2, 7)):
            observations = []
            for variant in engines:
                harness = args.omega_harness.resolve()
                result, rss = run(name, variant, harness / "run.metta", cwd=harness,
                                  overrides={"PYTHONPATH": str(harness), "HARNESS_STEPS": str(count),
                                             "HARNESS_ACTIVE_LOOPS": str(active), "HARNESS_MESSAGE_EVERY": str(every)})
                summary = records("harness-summary: ", result.stdout)
                gauges = records("symbol-table: ", result.stdout)
                if len(summary) != 1 or len(gauges) != 1:
                    raise AssertionError("missing synthetic loop receipt")
                observations.append(summary[0])
                receipt["omega"].append({"case": name, "engine": variant,
                                         "rss_kib": rss, "summary": summary[0], "gauges": gauges[0]})
                save()
            if observations[0] != observations[1]:
                raise AssertionError(f"Omega {name}: provider requests or ordered actions differ")
            print(f"PASS: Omega {name}; provider requests and ordered actions matched", flush=True)
    receipt["status"] = "passed"
    save()
    print("Engine trial receipt:", out / "results.json")


if __name__ == "__main__":
    main()
