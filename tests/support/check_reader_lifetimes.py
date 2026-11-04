#!/usr/bin/env python3
"""Pinned retained-reader controls across mutation and real collection points.

Cases keep work handles across source requests. Source-space removal and
replacement occur after each reader has delivered a prefix. Full original
bags, post-state and an unrelated owner have independent expected answers.
The debugger checks live provider registrations, exact callback sequences,
cache materialization and collection while snapshots remain registered.
"""
import argparse
from dataclasses import asdict
import json
from pathlib import Path

from check_native_observers import ROOT, Run, captured_run, compare, execute, route_environment, save_json, sha256
from native_observer_cases import MARKER
from native_observer_probes import REVIEWED_SHA256S, R82_SHA256
from check_relation_publication_faults import classify

LAYOUTS = {
    R82_SHA256: {"head": 16, "count": 24, "registry_len": 0x118, "registry_cap": 0x120},
    "2f00d0c875c00bac886ff6cff93a883d12a1b97f4939bed24354bd1eaa130b37":
        {"head": 24, "count": 32, "registry_len": 0x128, "registry_cap": 0x130},
    "4f5bd550a4c723e1c407cd81f4716f8b34ca17e69467b48d1eea60c44108d131":
        {"head": 24, "count": 32, "registry_len": 0x128, "registry_cap": 0x130},
    "d89724427430ecaae39ec4e712a711aa6748cea36c2af6a0b081dd3e5ad9e3c0":
        {"head": 24, "count": 32, "registry_len": 0x128, "registry_cap": 0x130},
}


def program(shape, handles):
    if shape not in ("deep", "flat") or not 1 <= handles <= 17:
        raise ValueError("unsupported lifecycle case")
    fact = lambda x: f"(edge {x} (box {x}))" if shape == "deep" else f"(edge {x} {x})"
    # Compute keys absent as literals from the source document. Otherwise
    # parser-held constants can incidentally retain the very terms under test.
    base = 1000003
    effect = lambda operation, x: f"!(let $key (+ {base} {x}) ({operation} &source {fact('$key')}))"
    pattern = "(edge $x (box $y))" if shape == "deep" else "(edge $x $y)"
    lines = ["!(bind! &source (new-space))", "!(bind! &answers (new-space))",
             "!(bind! &outside (new-space))",
             "!(let $value (+ 7000001 19) (add-atom &outside (outside $value)))"]
    lines += [effect("add-atom", x) for x in (0, 1, 2, 3, 1)]
    lines += ["(= (save-prefix (taken $xs $state)) (let $_ (collapse (let $a (superpose $xs) (add-atom &answers $a))) $state))"]
    for i in range(handles):
        lines += [f"!(let $h (work-new (match &source {pattern} (answer $x $y))) (add-atom &self (saved-work {i} $h)))",
                  f"!(match &self (saved-work {i} $h) (save-prefix (work-take $h 2)))"]
    lines += [effect("remove-atom", x) for x in (0, 1, 2, 3)]
    lines += [effect("add-atom", 9)]
    for i in range(handles):
        lines += [f"!(match &self (saved-work {i} $h) (save-prefix (work-take $h 20)))"]
    observations = ["(collapse (match &answers (answer $x $y) ($x $y)))",
                    f"(collapse (match &source {pattern} ($x $y)))",
                    "(collapse (match &outside (outside $x) $x))"]
    contracts = {MARKER + str(i): "bag" for i in range(len(observations))}
    lines += [f"!(let $result {body} ({MARKER}{i} $result))" for i, body in enumerate(observations)]
    for i in range(handles):
        lines += [f"!(match &self (saved-work {i} $h) (work-cancel $h))"]
    expected = ["(" + " ".join(f"({base+x} {base+x})" for _ in range(handles) for x in (0, 1, 2, 3, 1)) + ")",
                f"(({base+9} {base+9}))", "(7000020)"]
    return "\n".join(lines) + "\n", contracts, expected


def oracle(run, contracts, expected):
    # Use the existing typed comparator but keep the independent expected bag.
    from check_native_observers import observations
    try:
        _, effects = observations(run.stdout, contracts)
    except ValueError:
        return "protocol-mismatch"
    output = "".join(effects) + "".join(f"({marker} {value})\n" for marker, value in zip(contracts, expected))
    reference = Run("exited", 0, output, "", 0, {}, [])
    return compare(run, reference, contracts)


def lifecycle_evidence(receipt, collecting):
    events = receipt.get("events", [])
    valid = (receipt.get("architecture") == "i386:x86-64" and not receipt.get("errors")
             and receipt.get("pending") == 0 and receipt.get("registered_at_exit") == []
             and receipt.get("exit") == {"code": 0, "terminal": True}
             and all(e.get("returned") and not e.get("unwound") for e in events))
    added = [e for e in events if e["kind"] == "register" and e.get("result", 0) & 255]
    roots = [e for e in events if e["kind"] == "roots"]
    reads = [e for e in events if e["kind"] == "record-read"]
    active = [e for e in events if e["kind"] == "collect" and e.get("success") and e["active_contexts"]]
    complete = all(set(e["active_contexts"]) <= {r["context"] for r in roots if e["call"] in r["collections"]}
                   for e in active)
    cached = [r for r in roots if any(key != (1 << 64) - 1 for key in r["cache"])]
    qualified = (valid and bool(added) and bool(reads) and all(r.get("exact") for r in roots)
                 and all(r.get("matches_cache") and r.get("stable_existing") for r in reads)
                 and complete and (bool(active) and bool(cached) if collecting else not active))
    return {"qualified": qualified, "valid": valid, "registrations": len(added), "callbacks": len(roots),
            "cache_reads": len(reads), "materializations": sum(r["before"] == (1 << 64) - 1 for r in reads),
            "active_collections": len(active), "callbacks_with_cache": len(cached),
            "all_active_providers_visited": complete,
            "collection_sampling": receipt.get("sampling"),
            "registry_growths": sum(e["registry_len"] == e["registry_cap"] for e in added)}


def provider_fault(binary, identity, source, directory):
    """Fail the actual realloc under a kept-reader provider registration.

    r82's cetta_realloc preserves RCX before its allocator call; RCX is the
    registering callback in the reviewed parent. Failure aborts, rather than
    returning the unreachable defensive false branch to the provider caller.
    """
    directory.mkdir()
    config = {"name": "kept-provider-growth", "symbol": "cetta_realloc", "offset": 0x14, "nth": 1,
              "ancestor": "term_universe_add_root_provider",
              "symbol_registers": {"rcx": "space_kept_occurrences_roots"},
              "stdout": str(directory / "inferior.stdout"), "stderr": str(directory / "inferior.stderr"),
              "receipt": str(directory / "fault.json")}
    env = route_environment("eager", False)
    env["CETTA_TERM_UNIVERSE_COLLECTION"] = "off"
    env.pop("CETTA_TERM_UNIVERSE_COLLECTION_TRACE", None)
    env["CETTA_RELATION_FAULT_CONFIG"] = json.dumps(config)
    debugger = execute(["gdb", "-q", "-nx", "-nh", "-batch", "-iex", "set auto-load off",
        "-iex", "set debuginfod enabled off", "-x", str(Path(__file__).with_name("relation_fault_gdb.py")),
        "--args", str(binary), "--profile", "extended", "--lang", "petta", str(source)],
        cwd=ROOT, environment=env, timeout=60, output_limit=2*1024*1024, file_limit=2*1024*1024)
    save_json(directory / "debugger.json", asdict(debugger))
    try:
        receipt = json.loads(Path(config["receipt"]).read_text())
    except (OSError, ValueError):
        receipt = {"errors": ["missing-or-invalid-receipt"]}
    if debugger.status != "exited" or debugger.returncode != 0:
        receipt.setdefault("errors", []).append("debugger-execution-failed")
    outcome = classify(receipt, "unexpected-normal-completion")
    events = receipt.get("events", [])
    matched = (len(events) == 1 and events[0].get("matched_symbol_registers", {}).get("rcx", {}).get("symbol")
               == "space_kept_occurrences_roots")
    result = {"disposition": outcome, "passed": outcome == "fatal" and matched,
              "source_sha256": sha256(source), "receipt": receipt}
    save_json(directory / "result.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    identity = sha256(binary)
    if identity not in REVIEWED_SHA256S or identity not in LAYOUTS:
        raise SystemExit("no reviewed lifecycle profile for this executable")
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    support = Path(__file__).parent
    paths = [support / name for name in ("check_reader_lifetimes.py", "reader_lifecycle_gdb.py",
        "check_native_observers.py", "compare_answers.py", "native_observer_cases.py",
        "native_observer_probes.py", "native_observer_gdb.py",
        "check_relation_publication_faults.py", "relation_fault_gdb.py")]
    identities = {path.name: sha256(path) for path in paths}
    results = []
    for shape in ("deep", "flat"):
        for handles in (1, 17):
            source, contracts, expected = program(shape, handles)
            for mode in ("off", "eager"):
                for gc in ("off", "reclaim-every"):
                    directory = root / f"{shape}-{handles}-{mode}-{gc}"
                    directory.mkdir()
                    source_path = directory / "program.metta"
                    source_path.write_text(source)
                    source_hash = sha256(source_path)
                    command = [str(binary), "--profile", "extended", "--lang", "petta", str(source_path)]
                    env = route_environment(mode, False)
                    env["CETTA_TERM_UNIVERSE_COLLECTION"] = gc
                    env.pop("CETTA_TERM_UNIVERSE_COLLECTION_TRACE", None)
                    reference_env = dict(env, **{key: "1" for key in (
                        "CETTA_NATIVE_QUERY_REFERENCE", "CETTA_FOLD_ARITH_REFERENCE",
                        "CETTA_FLATTEN_FOLD_REFERENCE", "CETTA_COLLECTION_INGRESS_REFERENCE")})
                    native = execute(command, cwd=ROOT, environment=env, timeout=30, output_limit=2*1024*1024, dense_trace=True)
                    reference = execute(command, cwd=ROOT, environment=reference_env, timeout=30, output_limit=2*1024*1024, dense_trace=True)
                    laws = {"differential": compare(native, reference, contracts),
                            "native_oracle": oracle(native, contracts, expected),
                            "reference_oracle": oracle(reference, contracts, expected)}
                    evidence = None
                    if mode == "eager":
                        config = {"stdout": str(directory / "inferior.stdout"), "stderr": str(directory / "inferior.stderr"),
                                  "receipt": str(directory / "probe.json"), "event_limit": 20000,
                                  "layout": LAYOUTS[identity]}
                        debugger = execute(["gdb", "-q", "-nx", "-nh", "-batch", "-iex", "set auto-load off",
                            "-iex", "set debuginfod enabled off", "-x", str(support / "reader_lifecycle_gdb.py"), "--args"] + command,
                            cwd=ROOT, environment=dict(env, CETTA_READER_GDB_CONFIG=json.dumps(config)), timeout=180,
                            output_limit=16*1024*1024, file_limit=16*1024*1024)
                        save_json(directory / "debugger.json", asdict(debugger))
                        try:
                            receipt = json.loads(Path(config["receipt"]).read_text())
                        except (OSError, ValueError):
                            receipt = {"errors": ["missing-or-invalid-receipt"]}
                        if debugger.status != "exited" or debugger.returncode != 0:
                            receipt.setdefault("errors", []).append("debugger-execution-failed")
                        raw = {kind: Path(config[kind]).read_bytes() if Path(config[kind]).exists() else b"" for kind in ("stdout", "stderr")}
                        traced = captured_run(debugger.status, receipt.get("exit", {}).get("code"), raw, debugger.seconds, dense_trace=True)
                        laws["probe_observation"] = compare(traced, native, contracts)
                        evidence = lifecycle_evidence(receipt, gc != "off")
                        save_json(directory / "probe-observation.json", asdict(traced))
                    passed = (all(law == "equal" for law in laws.values()) and
                              (evidence is None or evidence["qualified"]) and sha256(source_path) == source_hash)
                    result = {"shape": shape, "handles": handles, "storage": mode, "gc": gc,
                              "source_sha256": source_hash, "contracts": contracts, "expected": expected,
                              "native": asdict(native), "reference": asdict(reference), "laws": laws,
                              "evidence": evidence, "passed": passed}
                    save_json(directory / "result.json", result)
                    results.append(result)
                    print(directory.name, "PASS" if passed else "FAIL", laws, evidence, flush=True)
    fault = provider_fault(binary, identity, root / "deep-17-eager-off/program.metta", root / "provider-growth-fault")
    print("provider-growth-fault", "PASS" if fault["passed"] else "FAIL", fault["disposition"], flush=True)
    stable = sha256(binary) == identity and identities == {path.name: sha256(path) for path in paths}
    growth = any(r["evidence"] and r["evidence"]["registry_growths"] for r in results)
    summary = {"binary_sha256": identity, "layout": LAYOUTS[identity],
               "identities_unchanged": stable, "source_files": identities,
               "registry_growth_observed": growth, "provider_failure": fault, "results": results}
    save_json(root / "summary.json", summary)
    return 0 if stable and growth and fault["passed"] and all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
