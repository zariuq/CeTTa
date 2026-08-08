#!/usr/bin/env python3
"""Cross-engine benchmark for three Nil chaining workloads.

Engines: SWI PeTTa, CeTTa PeTTa (extended profile), integrated RuleMachine
(bytecode + native for BFC; compiled-artifact runner for synthesis/SUMO),
and canonical MORK `bench bfc --bfc-size 13` for the Hilbert workload.

Protocol (per the pinned specification):
- source identities verified by sha256 before any timing; hard fail on drift;
- every displayed cell: nine serialized samples, median and range reported;
- every sample validates the complete answer set (test-harness check marks);
- PeTTa lanes: batch sizes 1/11/101/501, per-query cost estimated as the
  regression slope over batch size, so the shared startup+load intercept is
  excluded; check-mark counts must scale exactly with the batch;
- RuleMachine search rows come from single fresh processes per sample using
  `(system:timed (delay ...))` so laziness and the revision-pinned memo can
  neither skip nor pre-answer the computation;
- MORK reports its internal search elapsed and total process wall separately.

Raw results are written as JSON next to the fixtures (durable storage).
"""
from __future__ import annotations
import argparse, hashlib, json, pathlib, re, statistics, subprocess, tempfile, time

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "experiments/gslt2parse_foundation/migration_fixtures/nil_chaining"
GUESTS = ROOT / "tests/prime/nil_rule_machine_guests.generated.metta"

SOURCE_IDENTITIES = {
    "/home/aimama/aihub/repos/ngeiswei-chaining-run/experimental/backward-via-forward/bfc-xp.mm2":
        "b53b6079a03241f39fe7d8750b77247ce959a73c95dc55cc2b419d83df3ce5b1",
    "/home/aimama/aihub/repos/ngeiswei-chaining-run/experimental/synthesis/SynthesizeTest.metta":
        "ba3279cfbdd737c67b4118cd734c0f69e0dfcb87c0b4035a082c0d658d78ac3e",
    "/home/aimama/aihub/repos/ngeiswei-chaining-run/experimental/sumo/john-carry-flower/john-carry-flower.kif.metta":
        "a8df9448de199882f944e1010de42d5641a5ccf8829d1c6963fcd262f85007db",
    "/home/aimama/aihub/repos/ngeiswei-chaining-run/experimental/sumo/rule-base.metta":
        "9e6f4df984188e023400af78227132178996cfd0f38c4a8f3398fc085719ab55",
}

PETTA_FIXTURES = {
    # workload: (fixture, checks-per-query, batch ladder)
    # jarr is a hundreds-of-ms query: short ladder. The 1/11/101/501 ladder is
    # for sub-millisecond queries, per the specification.
    "hilbert_jarr13": ("nil_hilbert_obfc_jarr_petta_v1.metta", 1, [1, 3, 9]),
    "typed_synthesis": ("nil_typed_synthesis_petta_v1.metta", 1, [1, 11, 101, 501]),
    "sumo_depth4": ("nil_sumo_john_carry_flower_petta_v1.metta", 1, [1, 11, 101, 501]),
}

RM_SEARCH = {
    "hilbert_jarr13_bytecode":
        "(compile:rule-program-run (nil-bfc-rule-program) 13 1000000 10 (imp (imp (imp p q) r) (imp q r)))",
    "hilbert_jarr13_native":
        "(compile:rule-program-run-native (nil-bfc-rule-program) 13 1000000 10 (imp (imp (imp p q) r) (imp q r)))",
    "typed_synthesis_artifact":
        "(compile:run (nil-synthesis-r1-artifact) 2 10000 100 (arrow String Number Number))",
    "sumo_depth4_artifact":
        "(compile:run (nil-sumo-artifact) 4 1000000 100 (objectTransferred JohnsCarry JohnsFlower))",
}

def sha256(path: str) -> str:
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

def verify_identities() -> dict:
    out = {}
    for path, expected in SOURCE_IDENTITIES.items():
        actual = sha256(path)
        if actual != expected:
            raise SystemExit(f"IDENTITY MISMATCH: {path}\n  expected {expected}\n  actual   {actual}")
        out[path] = actual
    for name, _, _ladder in PETTA_FIXTURES.values():
        out[str(FIXTURES / name)] = sha256(str(FIXTURES / name))
    return out

def run(cmd, timeout=600, cwd=None):
    t0 = time.monotonic()
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=cwd)
    return time.monotonic() - t0, p.stdout + p.stderr

def marks(text):
    return text.count("✅"), text.count("❌")

def batch_file(fixture: pathlib.Path, n: int, tmpdir: str) -> pathlib.Path:
    text = fixture.read_text()
    idx = text.rindex("!(test")
    bang = text[idx:].strip()
    out = pathlib.Path(tmpdir) / f"{fixture.stem}_x{n}.metta"
    out.write_text(text + "\n" + "\n".join([bang] * (n - 1)) + "\n")
    return out

def eq_batch_file(fixture: pathlib.Path, n: int, tmpdir: str, tag: str) -> pathlib.Path:
    """he/prime lanes have no `test` builtin: rewrite the fixture's final
    test bang into an equality check that prints True per query (== also
    demands its arguments, defeating call-by-need)."""
    text = fixture.read_text()
    idx = text.rindex("!(test")
    head, bang = text[:idx], text[idx:].strip()
    inner = bang[len("!(test"):].strip()[:-1]
    depth = 0; split = None
    for i, ch in enumerate(inner):
        if ch == "(": depth += 1
        elif ch == ")": depth -= 1
        elif ch.isspace() and depth == 0 and i > 0: split = i; break
    query, expected = inner[:split], inner[split:].strip()
    eq = f"!(== {query} {expected})"
    out = pathlib.Path(tmpdir) / f"{fixture.stem}_{tag}_x{n}.metta"
    out.write_text(head + "\n" + "\n".join([eq] * n) + "\n")
    return out

def eq_lane(engine_name, invoke, fixture, samples, batches, tmpdir, tag):
    rows = {}
    for n in batches:
        f = eq_batch_file(fixture, n, tmpdir, tag)
        walls = []
        for _ in range(samples):
            wall, out = run(invoke + [str(f)])
            trues = out.count("[True]") + out.count(", True") + out.count("True]")
            trues = out.count("True")
            if "False" in out or trues < n:
                raise SystemExit(
                    f"VALIDATION FAILURE {engine_name} {fixture.name} batch={n}: True={trues} expected {n}\n{out[-400:]}")
            walls.append(wall)
        rows[n] = {"median_s": statistics.median(walls), "min_s": min(walls), "max_s": max(walls),
                   "checks_per_run": n}
    lo, hi = min(batches), max(batches)
    slope_ms = (rows[hi]["median_s"] - rows[lo]["median_s"]) / (hi - lo) * 1000.0
    return {"batches": rows, "per_query_ms_slope": slope_ms,
            "startup_load_intercept_s": rows[lo]["median_s"] - slope_ms / 1000.0 * lo,
            "validation": "==-True count per query"}

def petta_lane(engine_name, invoke, fixture, expected_per_batch, samples, batches, tmpdir):
    rows = {}
    for n in batches:
        f = batch_file(fixture, n, tmpdir)
        walls = []
        for _ in range(samples):
            wall, out = run(invoke + [str(f)])
            ok, bad = marks(out)
            if bad != 0 or ok != n * expected_per_batch:
                raise SystemExit(
                    f"VALIDATION FAILURE {engine_name} {fixture.name} batch={n}: {ok}✅ {bad}❌ (expected {n})")
            walls.append(wall)
        rows[n] = {"median_s": statistics.median(walls), "min_s": min(walls), "max_s": max(walls),
                   "checks_per_run": n * expected_per_batch}
    lo, hi = min(batches), max(batches)
    slope_ms = (rows[hi]["median_s"] - rows[lo]["median_s"]) / (hi - lo) * 1000.0
    return {"batches": rows, "per_query_ms_slope": slope_ms,
            "startup_load_intercept_s": rows[lo]["median_s"] - slope_ms / 1000.0 * lo}

def rulemachine_search(cetta, expr, samples, tmpdir):
    guests = "\n".join(l for l in GUESTS.read_text().splitlines() if not l.startswith("!("))
    program = ("!(import! &self system)\n" + guests +
               f"\n!(system:timed (delay {expr}))\n")
    f = pathlib.Path(tmpdir) / (re.sub(r"[^a-z0-9]+", "_", expr[:40]) + ".metta")
    f.write_text(program)
    vals, walls, answers = [], [], set()
    for _ in range(samples):
        wall, out = run([cetta, "--lang", "prime", str(f)])
        m = re.search(r"system:timed-result (\d+)", out)
        if not m:
            raise SystemExit(f"NO TIMED RESULT for {expr}\n{out[-500:]}")
        a = re.search(r"system:timed-result \d+ (.*)", out)
        answers.add(hashlib.sha256(a.group(1).encode()).hexdigest()[:16])
        vals.append(int(m.group(1)) / 1e6)
        walls.append(wall)
    if len(answers) != 1:
        raise SystemExit(f"ANSWER INSTABILITY for {expr}: {answers}")
    return {"median_ms": statistics.median(vals), "min_ms": min(vals), "max_ms": max(vals),
            "process_wall_median_s": statistics.median(walls),
            "answer_hash16": answers.pop(), "fresh_process_per_sample": True}

def mork_lane(mork, samples):
    vals, walls, proofs = [], [], set()
    for _ in range(samples):
        wall, out = run([mork, "bench", "bfc", "--bfc-size", "13"])
        n = out.count("(C (")
        if n != 2:
            raise SystemExit(f"MORK VALIDATION FAILURE: {n} proofs, expected 2")
        proofs.add(hashlib.sha256("".join(sorted(re.findall(r"\(C \(.*", out))).encode()).hexdigest()[:16])
        m = re.search(r"elapsed (\d+)", out)
        vals.append(int(m.group(1)))
        walls.append(wall)
    if len(proofs) != 1:
        raise SystemExit(f"MORK ANSWER INSTABILITY: {proofs}")
    return {"internal_search_median_ms": statistics.median(vals),
            "internal_min_ms": min(vals), "internal_max_ms": max(vals),
            "process_wall_median_s": statistics.median(walls), "proofs": 2,
            "answer_hash16": proofs.pop()}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cetta", default=str(ROOT / "cetta"))
    ap.add_argument("--swi-petta", default="/home/aimama/aihub/hyperon/PeTTa/run.sh")  # invoked via bash (no shebang)
    ap.add_argument("--mork", default="/home/aimama/aihub/hyperon/MORK/target/release/mork")
    ap.add_argument("--samples", type=int, default=9)
    ap.add_argument("--batches", type=int, nargs="+", default=[1, 11, 101, 501])
    ap.add_argument("--out", default=str(FIXTURES / "results"))
    args = ap.parse_args()

    results = {"schema": "cetta.nil-chaining-crossbench.v1",
               "identities": verify_identities(),
               "cetta_binary_sha256": sha256(args.cetta),
               "samples_per_cell": args.samples, "batches": args.batches,
               "lanes": {}}
    with tempfile.TemporaryDirectory() as tmp:
        for wl, (fname, expected, ladder) in PETTA_FIXTURES.items():
            fx = FIXTURES / fname
            results["lanes"][f"swi_petta/{wl}"] = petta_lane(
                "swi", ["bash", args.swi_petta], fx, expected, args.samples, ladder, tmp)
            results["lanes"][f"cetta_petta/{wl}"] = petta_lane(
                "cetta", [args.cetta, "--lang", "petta", "--profile", "extended"],
                fx, expected, args.samples, ladder, tmp)
            results["lanes"][f"cetta_he_extended/{wl}"] = eq_lane(
                "cetta-he-ext", [args.cetta, "--profile", "he-extended"],
                fx, args.samples, ladder, tmp, "heext")
            results["lanes"][f"cetta_prime/{wl}"] = eq_lane(
                "cetta-prime", [args.cetta, "--lang", "prime"],
                fx, args.samples, ladder, tmp, "prime")
        for name, expr in RM_SEARCH.items():
            results["lanes"][f"rulemachine/{name}"] = rulemachine_search(
                args.cetta, expr, args.samples, tmp)
        results["lanes"]["mork_mm2/hilbert_jarr13"] = mork_lane(args.mork, args.samples)

    outdir = pathlib.Path(args.out); outdir.mkdir(exist_ok=True)
    stamp = time.strftime("%Y%m%dT%H%M%S")
    outfile = outdir / f"nil_chaining_crossbench_{stamp}.json"
    outfile.write_text(json.dumps(results, indent=1))
    print(json.dumps({k: {kk: vv for kk, vv in v.items() if kk in
                          ("per_query_ms_slope", "median_ms", "internal_search_median_ms")}
                      for k, v in results["lanes"].items()}, indent=1))
    print(f"RAW: {outfile}")

if __name__ == "__main__":
    main()
