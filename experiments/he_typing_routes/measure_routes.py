#!/usr/bin/env python3
"""Time every route on HE's typing corpus and measure the size of its encoding.

For each judgment programme the routes run twice: once on the whole programme (a batch) and
once per judgment, on the programme with that judgment's query alone.  A route that starts a
checker process also runs once with no judgment; its time is the start-up cost, reported
beside the times.  Sizes are counted on the text each route checks: declarations and laws of
the theory, and the size of the proof or term.

Usage: measure_routes.py --work DIR --json OUT [--upstream-root DIR] [--mm0-tree DIR]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import corpus  # noqa: E402
import fo_schemes  # noqa: E402
import run_routes  # noqa: E402

MM0_RS = Path("/home/aimama/aihub/repos/mm0/mm0-rs/target/release/mm0-rs")
MM0_C = Path("/home/aimama/aihub/tmp/nik-vibe-mm0-20261002/C/upstream-baseline-v1/mm0-c")


def single(programme: corpus.Programme, ident: str) -> str:
    """The programme with one judgment's query and every other line kept."""
    keep = [line for line in programme.lines
            if "(Judgment " not in line or f"(Judgment {ident} " in line]
    return "\n".join(keep) + "\n"


def empty(programme: corpus.Programme) -> str:
    return "\n".join(line for line in programme.lines if "(Judgment " not in line) + "\n"


def timed(fn, *arguments):
    start = time.perf_counter()
    result = fn(*arguments)
    return result, time.perf_counter() - start


def mm0_check(args, path: Path, metta: bool):
    """Generate the programme's MM0 theory, compile it, and check it."""
    import mm0_flat
    theory, planned = mm0_flat.programme_theory(list(run_routes.fo_judgments(path)))
    mm1 = path.with_suffix(".mm1")
    text = theory.text()
    mm1.write_text(text)
    spec = re.sub(r"pub (theorem [^=]*?\$[^$]*\$)\s*=\s*'[^;]*;", r"\1;", text)
    spec_path = path.with_suffix(".mm0")
    spec_path.write_text(spec)
    compile_start = time.perf_counter()
    subprocess.run([str(MM0_RS), "compile", str(mm1), str(path.with_suffix(".mmb"))],
                   capture_output=True, text=True)
    subprocess.run([str(MM0_RS), "compile", str(mm1), str(path.with_suffix(".mmu"))],
                   capture_output=True, text=True)
    compile_seconds = time.perf_counter() - compile_start
    start = time.perf_counter()
    with open(spec_path) as spec_file:
        verified = subprocess.run([str(MM0_C), str(path.with_suffix(".mmb"))], stdin=spec_file,
                                  capture_output=True, text=True).returncode == 0
    c_seconds = time.perf_counter() - start
    metta_seconds = None
    metta_verified = None
    if metta:
        out = path.with_suffix(".metta-mm0")
        subprocess.run(["rm", "-rf", str(out)])
        start = time.perf_counter()
        run = subprocess.run(
            ["bash", "-c", "ulimit -v 25165824; exec bash tools/check_mm0_native.sh "
             f"--binary runtime/cetta-mm0-core-v1 --spec {spec_path} --proof "
             f"{path.with_suffix('.mmu')} --output {out} --timeout 900"],
            cwd=args.mm0_tree, capture_output=True, text=True)
        metta_seconds = time.perf_counter() - start
        metta_verified = run.stdout.strip().endswith("true")
    axioms = sum(1 for line in text.splitlines() if line.startswith("axiom "))
    terms = sum(1 for line in text.splitlines() if line.startswith("term "))
    theorems = [p for p in planned if p[2]]
    sizes = {"spec_lines": len(spec.splitlines()), "axioms": axioms, "terms": terms,
             "theorems": len(theorems), "chars": len(text)}
    answers = {}
    for ident, k, name, typ, why in planned:
        answers.setdefault(ident, []).append((name, typ))
    return {"verified": verified, "metta_verified": metta_verified, "compile": compile_seconds,
            "mm0c": c_seconds, "metta": metta_seconds, "sizes": sizes, "answers": answers}


def encoding_sizes(path: Path) -> dict:
    """Per-judgment sizes of the encodings each route checks."""
    import encoders
    import hol_flat
    import hotg_closure
    sizes = {"hol-flat": [], "hotg": [], "lf": []}
    for ident, space, expression, typing in run_routes.fo_judgments(path):
        status, checks, lines = hol_flat.judgment_checks(space, expression)
        for _, text in lines:
            sizes["hol-flat"].append({"declarations": text.count("(HOL:Step "),
                                      "chars": len(text)})
        hstatus, hchecks = hotg_closure.judgment_checks(space, expression)
        if hstatus == "typed":
            for k, th, statement, proof, typ, mutant in hchecks:
                if th is not None:
                    sizes["hotg"].append({"axioms": len(th.axioms), "constants": len(th.constants),
                                          "proof_chars": len(proof),
                                          "chars": sum(len(l) for l in th.text("&x")) + len(proof)})
        elif hstatus == "refuted":
            th, statement, proof = hchecks[0]
            sizes["hotg"].append({"axioms": len(th.axioms), "constants": len(th.constants),
                                  "proof_chars": len(proof), "refutation": True,
                                  "chars": sum(len(l) for l in th.text("&x")) + len(proof)})
        for derivation in typing.derivations:
            if encoders.unsupported(derivation) or encoders.gradual(derivation):
                continue
            try:
                signature, term, typ = encoders.lf_judgment(space, derivation)
            except ValueError:
                continue
            sizes["lf"].append({"declarations": signature.count("(DConst "),
                                "chars": len(signature) + len(term) + len(typ)})
    return sizes


def median(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--upstream-root", type=Path,
                        default=run_routes.ROOT.parent / "hyperon-experimental")
    parser.add_argument("--kernel-root", type=Path,
                        default=run_routes.ROOT.parents[1] / "Mettapedia" / "MettaKernel" / "kernel")
    parser.add_argument("--mm0-tree", type=Path,
                        default=run_routes.ROOT.parent / "cetta-mm0-metta-20261004")
    parser.add_argument("--no-metta-mm0", action="store_true")
    args = parser.parse_args()
    args.probe = False
    args.work.mkdir(parents=True, exist_ok=True)
    routes = {
        "fo-schemes": lambda a, p: run_routes.fo_route(p),
        "lf": lambda a, p: run_routes.guest_route(a, p, "lf"),
        "hotg": run_routes.hotg_route,
        "hol-flat": run_routes.hol_flat_route,
        "prime-hol": lambda a, p: run_routes.cetta(["--lang", "prime"], p),
        "he-upstream": lambda a, p: run_routes.he_upstream(a.upstream_root, p),
    }
    binary = hashlib.sha256((run_routes.ROOT / "cetta").read_bytes()).hexdigest()[:16]
    record = {"binary": binary, "load_before": os.getloadavg(), "programmes": []}
    for programme in run_routes.programmes(args):
        entry = {"name": programme.name, "judgments": len(programme.judgments),
                 "batch": {}, "startup": {}, "per_judgment": {r: [] for r in routes}}
        path = args.work / f"{programme.name}.metta"
        path.write_text(programme.text())
        for name, route in routes.items():
            _, seconds = timed(route, args, path)
            entry["batch"][name] = seconds
        empty_path = args.work / f"{programme.name}.empty.metta"
        empty_path.write_text(empty(programme))
        for name in ("prime-hol", "he-upstream"):
            _, seconds = timed(routes[name], args, empty_path)
            entry["startup"][name] = seconds
        for judgment in programme.judgments:
            one = args.work / f"{programme.name}.{judgment.ident}.metta"
            one.write_text(single(programme, judgment.ident))
            for name, route in routes.items():
                _, seconds = timed(route, args, one)
                entry["per_judgment"][name].append(seconds)
        mm0 = mm0_check(args, path, not args.no_metta_mm0)
        entry["mm0"] = mm0
        entry["sizes"] = encoding_sizes(path)
        record["programmes"].append(entry)
        print(programme.name, {k: round(v, 2) for k, v in entry["batch"].items()},
              "mm0-c", round(mm0["mm0c"], 3), "metta-mm0", mm0["metta"], flush=True)
    record["load_after"] = os.getloadavg()
    args.json.write_text(json.dumps(record, indent=1, default=str))
    # Summary.
    print("load average before", record["load_before"], "after", record["load_after"])
    print(f"binary {binary}")
    for name in routes:
        per = [s for p in record["programmes"] for s in p["per_judgment"][name]]
        batch = [p["batch"][name] for p in record["programmes"]]
        print(f"{name:12s} median per judgment {median(per):.3f}s  median per batch "
              f"{median(batch):.3f}s  total batch {sum(batch):.2f}s")
    mm0c = [p["mm0"]["mm0c"] for p in record["programmes"]]
    metta = [p["mm0"]["metta"] for p in record["programmes"] if p["mm0"]["metta"] is not None]
    print(f"mm0-flat     mm0-c median per batch {median(mm0c):.4f}s; MeTTa MM0 checker median per "
          f"batch {median(metta) if metta else None}")


if __name__ == "__main__":
    main()
