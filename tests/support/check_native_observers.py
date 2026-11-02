#!/usr/bin/env python3
"""Observer-directed native/reference qualification with durable reproducers.

The output directory is new for each run. A successful comparison does not
by itself establish route activation. Without --qualified and a frozen binary
hash, runs are explicitly provisional. Timeouts, output limits, crashes and
reference failures never become equal empty answers.
"""
import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
import os
import re
import resource
from pathlib import Path
import selectors
import signal
import subprocess
import time

from compare_answers import alpha_tokens, collapse_equal, split_elements
from native_observer_cases import Case, Expr, FAMILIES, MARKER, OBSERVERS, ROUTE_COUNTERS, SOURCE_ANCHORS, generate, integer, lean_controls
from native_observer_probes import REVIEWED_SHA256S, REQUIRED_SUBROUTES, debugger_coverage, run_probe

ROOT = Path(__file__).resolve().parents[2]
REFERENCE_FLAGS = ("CETTA_NATIVE_QUERY_REFERENCE", "CETTA_FOLD_ARITH_REFERENCE",
                   "CETTA_FLATTEN_FOLD_REFERENCE", "CETTA_COLLECTION_INGRESS_REFERENCE")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


@dataclass
class Run:
    status: str
    returncode: int
    stdout: str
    stderr: str
    seconds: float
    counters: dict
    diagnostics: list


def execute(command, *, cwd, environment, timeout, output_limit, stats=False, dense_trace=False, file_limit=None):
    start = time.monotonic()
    def limit_files():
        resource.setrlimit(resource.RLIMIT_FSIZE, (file_limit, file_limit))
    process = subprocess.Popen(command, cwd=cwd, env=environment, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, start_new_session=True,
                               preexec_fn=limit_files if file_limit is not None else None)
    chunks = {"stdout": [], "stderr": []}
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ, "stdout")
    selector.register(process.stderr, selectors.EVENT_READ, "stderr")
    count = 0
    status = "exited"
    try:
        while selector.get_map():
            remaining = timeout - (time.monotonic() - start)
            if remaining <= 0:
                status = "timeout"
                break
            for key, _ in selector.select(min(remaining, 0.1)):
                data = os.read(key.fileobj.fileno(), 65536)
                if not data:
                    selector.unregister(key.fileobj)
                    continue
                count += len(data)
                if count > output_limit:
                    status = "output-limit"
                    break
                chunks[key.data].append(data)
            if status != "exited":
                break
        if status != "exited":
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        remaining = max(0.01, timeout - (time.monotonic() - start))
        try:
            process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            status = "timeout"
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
    finally:
        selector.close()
        process.stdout.close()
        process.stderr.close()
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
    return captured_run(status, process.returncode, {key: b"".join(value) for key, value in chunks.items()},
                        time.monotonic() - start, stats=stats, dense_trace=dense_trace)


def captured_run(status, returncode, chunks, seconds, *, stats=False, dense_trace=False):
    text = {}
    for key, raw in chunks.items():
        try:
            text[key] = raw.decode("utf-8")
        except UnicodeDecodeError:
            status = "invalid-utf8"
            text[key] = "bytes-hex:" + raw.hex()
    counters, diagnostics, errors = {}, [], []
    for line in text["stderr"].splitlines(keepends=True):
        parts = line.split()
        if stats and len(parts) == 3 and parts[0] == "runtime-counter" and parts[2].isdigit():
            counters[parts[1]] = int(parts[2])
        elif dense_trace and line.startswith("space relations: "):
            diagnostics.append(line.rstrip("\n"))
        else:
            errors.append(line)
    return Run(status, returncode, text["stdout"], "".join(errors), seconds, counters, diagnostics)


def single_atom(value):
    depth = roots = i = 0
    while i < len(value):
        char = value[i]
        if char.isspace():
            i += 1
            continue
        if char == ")":
            depth -= 1
            if depth < 0:
                return False
            i += 1
            continue
        if depth == 0:
            roots += 1
        if char == "(":
            depth += 1
            i += 1
        elif char == '"':
            i += 1
            while i < len(value) and value[i] != '"':
                i += 2 if value[i] == "\\" else 1
            if i >= len(value):
                return False
            i += 1
        else:
            while i < len(value) and not value[i].isspace() and value[i] not in '()"':
                i += 1
    return depth == 0 and roots == 1


def observations(output, contracts):
    packets = {marker: [] for marker in contracts}
    other = []
    for line in output.splitlines(keepends=True):
        stripped = line.strip()
        parts = split_elements(stripped[1:-1], petta=True) if stripped.startswith("(") and stripped.endswith(")") else []
        if parts and parts[0].startswith(MARKER):
            if parts[0] not in contracts or len(parts) != 2 or not single_atom(stripped):
                raise ValueError("malformed or unknown observation packet")
            packets[parts[0]].append(parts[1])
        else:
            other.append(line)
    for marker, mode in contracts.items():
        if mode in ("bag", "ordered") and len(packets[marker]) != 1:
            raise ValueError(f"{marker} must publish one collected value")
    return packets, other


def compare(native, reference, contracts):
    if native.status != "exited" or reference.status != "exited":
        return "inconclusive-execution-limit"
    if reference.returncode != 0:
        return "invalid-reference"  # Generated cases are completed/caught observations.
    if native.returncode != 0:
        return "native-exit-mismatch"
    if native.stderr != reference.stderr:
        return "stderr-mismatch"
    try:
        left, leffects = observations(native.stdout, contracts)
        right, reffects = observations(reference.stdout, contracts)
    except ValueError:
        return "protocol-mismatch"
    if leffects != reffects:
        return "effect-trace-mismatch"
    for marker, mode in contracts.items():
        a, b = left[marker], right[marker]
        if mode == "bag":
            same = collapse_equal(a[0], b[0])
        elif mode == "answers":
            same = collapse_equal("(" + " ".join(a) + ")", "(" + " ".join(b) + ")")
        elif mode == "ordered":
            same = alpha_tokens(a[0]) == alpha_tokens(b[0])
        else:
            raise ValueError("unknown observation contract")
        if not same:
            return "value-mismatch"
    return "equal"


def route_environment(mode, reference):
    # A qualification run must not inherit unrelated runtime experiments.
    # Specialized drivers explicitly add their own settings after this reset.
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("CETTA_")}
    env["CETTA_SPACE_DENSE"] = mode
    env["CETTA_SPACE_DENSE_TRACE"] = "1"
    for key in REFERENCE_FLAGS:
        env[key] = "1" if reference else "0"
    return env


def coverage(case, native, reference):
    exact = {key: native.counters.get(key, 0) for key in ROUTE_COUNTERS.get(case.family, ())}
    confirmed = any(value > 0 and reference.counters.get(key, 0) == 0 for key, value in exact.items())
    takeover = case.family == "row-takeover" and any(
        re.fullmatch(r"space relations: [1-9][0-9]* rows move into relations", line) for line in native.diagnostics)
    subroutes = [key.removeprefix("native-fold-") for key, value in exact.items()
                 if value > 0 and reference.counters.get(key, 0) == 0]
    return {"level": "route-counter-confirmed" if confirmed else "operation-trace-confirmed" if takeover else "unconfirmed",
            "subroutes": subroutes,
            "exact_counters": exact,
            "generic_query_admissions": native.counters.get("native-query-admission", 0),
            "storage_diagnostics": native.diagnostics,
            "note": "Generic query/storage evidence is not proof of this specialization's activation."}


def nonempty_witness(case, run, contracts):
    """Known positive schemas must actually produce their designated witness.

    General filtered tests may legitimately reject every row. Saturation
    witnesses are final stored facts, not the potentially empty firing answers.
    This is a generator validity check, separate from native/reference equality.
    """
    if case.negative:
        return True
    if case.family == "filtered-match" and case.expr.render() != "(== $x $x)":
        return True
    if run.returncode != 0 or run.status != "exited":
        return False
    try:
        packets, _ = observations(run.stdout, contracts)
    except ValueError:
        return False
    marker = MARKER + ("1" if case.family in ("dead-rule", "saturation") else "0")
    values = packets[marker]
    if not values:
        return False
    if contracts[marker] in ("bag", "ordered"):
        return values[0] != "()"
    return True


def verdict_with_probe(verdict, probe_valid):
    # A failed coverage probe must never conceal an already observed runtime
    # discrepancy. Record the probe failure separately from that discrepancy.
    return "inconclusive-route-probe" if verdict == "equal" and not probe_valid else verdict


def evaluate_case(case, binary, mode, directory, options):
    source, contracts = case.render()
    path = directory / "program.metta"
    path.write_text(source)
    command = [str(binary), "--profile", "extended", "--lang", "petta"]
    if options.stats:
        command.append("--emit-runtime-stats")
    results = []
    environments = []
    for reference in (False, True):
        environment = route_environment(mode, reference)
        environments.append({key: value for key, value in environment.items()
                             if key.startswith("CETTA_") or key in ("LANG", "LC_ALL", "LC_NUMERIC")})
        result = execute(command + [str(path)], cwd=ROOT, environment=environment,
                         timeout=options.timeout, output_limit=options.output_limit,
                         stats=options.stats, dense_trace=True)
        results.append(result)
    native, reference = results
    verdict = compare(native, reference, contracts)
    if verdict == "equal" and not nonempty_witness(case, reference, contracts):
        verdict = "invalid-reference-witness"
    boundary_laws = [boundary_oracle(case, run, contracts) for run in (native, reference)]
    fold_laws = [fold_oracle(case, run, contracts) for run in (native, reference)]
    existence_laws = [existence_oracle(case, run, contracts) for run in (native, reference)]
    if verdict == "equal" and "value-mismatch" in (boundary_laws[1], fold_laws[1], existence_laws[1]):
        verdict = "invalid-reference-oracle"
    elif verdict == "equal" and "value-mismatch" in (boundary_laws[0], fold_laws[0], existence_laws[0]):
        verdict = "native-oracle-mismatch"
    record = {"case": case.to_dict(), "mode": mode, "source_sha256": sha256(path),
              "runtime_environments": environments,
              "contracts": contracts, "verdict": verdict, "coverage": coverage(case, native, reference),
              "boundary_oracles": boundary_laws,
              "fold_oracles": fold_laws,
              "existence_oracles": existence_laws,
              "native": asdict(native), "reference": asdict(reference)}
    if options.gdb_probe and case.family != "row-takeover":
        probes, laws = [], []
        for is_reference, original in ((False, native), (True, reference)):
            traced = run_probe(case.family, options.expected_sha256, command + [str(path)],
                               directory / ("probe-reference" if is_reference else "probe-native"),
                               route_environment(mode, is_reference), execute, cwd=ROOT,
                               timeout=options.probe_timeout, output_limit=options.output_limit)
            receipt, out_path, err_path = traced
            raw = {"stdout": out_path.read_bytes() if out_path.exists() else b"",
                   "stderr": err_path.read_bytes() if err_path.exists() else b""}
            status = "exited" if all(len(value) <= options.output_limit for value in raw.values()) else "output-limit"
            probed = captured_run(status, receipt.get("exit", {}).get("code"), raw, 0,
                                  stats=options.stats, dense_trace=True)
            law = compare(probed, original, contracts)
            probes.append(receipt)
            laws.append(law)
            save_json(out_path.parent / "observation.json", {"law": law, "run": asdict(probed)})
        evidence = debugger_coverage(case.family, *probes, *laws)
        record["debugger_coverage"] = evidence
        record["verdict"] = verdict_with_probe(record["verdict"], evidence["probe_valid"])
        if evidence["level"] != "unconfirmed":
            record["coverage"] = {**record["coverage"], **evidence}
    save_json(directory / "result.json", record)
    return record


def shrink(case, evaluate, budget):
    """Greedy fixed-point reduction; candidates must reproduce twice.

    The evaluator returns the same observer/route-failure fingerprint or None.
    Reaching the budget is reported explicitly, never called minimal.
    """
    current = case
    initial = evaluate(current)
    if initial is None or evaluate(current) != initial:
        return current, {"status": "not-reproducible", "attempts": 1 if initial is None else 2}
    attempts = 2
    seen = {current.key()}
    while True:
        improved = False
        for candidate in current.reductions():
            if candidate.key() in seen or candidate.render()[0] == current.render()[0]:
                continue
            if attempts + 2 > budget:
                return current, {"status": "budget-limited", "attempts": attempts}
            seen.add(candidate.key())
            first = evaluate(candidate)
            attempts += 1
            if first != initial:
                continue
            second = evaluate(candidate)
            attempts += 1
            if second == initial:
                current = candidate
                improved = True
                break
        if not improved:
            return current, {"status": "local-fixed-point", "attempts": attempts}


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("binary", type=Path)
    result.add_argument("--out", type=Path, required=True)
    result.add_argument("--seed", type=int, default=20261101)
    result.add_argument("--per-family", type=int, default=6)
    result.add_argument("--families", nargs="+", choices=FAMILIES, default=list(FAMILIES))
    result.add_argument("--modes", nargs="+", choices=("off", "on", "eager"), default=["on", "eager"])
    result.add_argument("--timeout", type=float, default=15)
    result.add_argument("--output-limit", type=int, default=2 * 1024 * 1024)
    result.add_argument("--stats", action="store_true")
    result.add_argument("--gdb-probe", action="store_true", help="hash-pinned worker-return probes; no runtime edits")
    result.add_argument("--probe-timeout", type=float, default=45)
    result.add_argument("--expected-sha256")
    result.add_argument("--qualified", action="store_true")
    result.add_argument("--shrink-budget", type=int, default=0)
    result.add_argument("--emit-lean", action="store_true")
    result.add_argument("--no-metamorphic", action="store_true")
    result.add_argument("--replay", type=Path, help="saved case object, case list or minimized.json")
    return result


def constant_oracle(observer, value):
    packet = lambda index, payload: f"({MARKER}{index} {payload})\n"
    if observer == "value":
        return packet(0, value)
    if observer == "tuple":
        return packet(0, f"(field {value} (ordered 1 0))")
    if observer in ("bag", "catch"):
        return packet(0, f"({value})")
    if observer == "ordered":
        return packet(0, f"((sort-key {json.dumps(value)} {value}))")
    if observer == "effect":
        return f"(observed-effect {value})\n" + packet(0, f"({value})") + packet(1, f"({value})")
    raise ValueError("unknown observer")


def boundary_oracle(case, run, contracts):
    """Independent finite counts for the explicitly generated boundary controls.

    These check designated result packets in addition to (not instead of)
    the full differential comparison. General mixed numeric matching is not
    silently assigned Python's equality semantics.
    """
    if case.boundary == "ordinary" or case.consumer != "count":
        return "not-applicable"
    if run.status != "exited" or run.returncode != 0:
        return "value-mismatch"
    try:
        packets, _ = observations(run.stdout, contracts)
    except ValueError:
        return "value-mismatch"
    if case.boundary == "incomplete-coverage":
        return "equal" if packets.get(MARKER + "1") == ["(3)"] else "value-mismatch"
    if not all(re.fullmatch(r"-?[0-9]+", value) for value in case.values):
        return "not-applicable"
    values = [str(int(value)) for value in case.values]
    expected = {}
    if case.boundary == "empty-group":
        missing = "missing-observer-key"
        keys = values[::2] + [missing]
        expected[MARKER + "1"] = "(" + " ".join(f"({key} {values.count(key)})" for key in keys) + ")"
    elif case.boundary == "ground-first":
        hits = [f"(open-hit {value} (pair {value} {value}))" for value in sorted(set(values))]
        if "1" in values:
            hits.append("(open-hit 1 (pair 1 2))")
        expected[MARKER + "1"] = "(" + " ".join(hits) + ")"
    elif case.boundary == "preheld-guard":
        value = str(len(set(values)) - 1)
        oracle, _ = observations(constant_oracle(case.observer, value),
                                 {key: mode for key, mode in contracts.items() if key == MARKER + "0" or
                                  (case.observer == "effect" and key == MARKER + "1")})
        actual = packets[MARKER + "0"]
        wanted = oracle[MARKER + "0"]
        if contracts[MARKER + "0"] == "bag":
            return "equal" if collapse_equal(actual[0], wanted[0]) else "value-mismatch"
        return "equal" if actual == wanted else "value-mismatch"
    return "equal" if all(len(packets.get(marker, [])) == 1 and collapse_equal(packets[marker][0], value)
                          for marker, value in expected.items()) else "value-mismatch"


def fold_oracle(case, run, contracts):
    """Exact finite sums for the additive subroute controls, not general eval."""
    if case.family != "arithmetic-fold" or case.consumer != "stream" or case.expr.render() != "(+ $a $x)":
        return "not-applicable"
    if case.producer == "run":
        value = 2 * len(case.values)
    elif all(re.fullmatch(r"-?[0-9]+", item) for item in case.values):
        value = sum(int(item) for item in case.values) * (2 if case.producer == "moments" else 1)
    else:
        return "not-applicable"
    oracle = Run("exited", 0, constant_oracle(case.observer, str(value)), "", 0.0, {}, [])
    return "equal" if compare(run, oracle, contracts) == "equal" else "value-mismatch"


def existence_oracle(case, run, contracts):
    if case.family != "match-existence":
        return "not-applicable"
    additions = 1 if case.boundary == "open-existence" else len(case.values)
    value = constant_oracle(case.observer, "true" if case.negative else "false")
    # println! is emitted immediately; top-level return packets are printed
    # later by this executable. Keep that presentation order explicit.
    if case.observer == "effect":
        printed, _, packets = value.partition("\n")
        expected = printed + "\n" + "true\n" * additions + packets
    else:
        expected = "true\n" * additions + value
    return "equal" if compare(run, Run("exited", 0, expected, "", 0, {}, []), contracts) == "equal" else "value-mismatch"


def main(argv=None):
    options = parser().parse_args(argv)
    if options.per_family < 1 or options.timeout <= 0 or options.output_limit <= 0 or options.shrink_budget < 0 or options.shrink_budget == 1:
        raise SystemExit("positive case/time/output bounds required")
    binary = options.binary.resolve(strict=True)
    identity = sha256(binary)
    if options.expected_sha256 and identity != options.expected_sha256:
        raise SystemExit("binary hash does not match requested identity")
    if options.qualified and not options.expected_sha256:
        raise SystemExit("qualification requires an expected frozen binary hash")
    if options.gdb_probe and (options.expected_sha256 not in REVIEWED_SHA256S or options.probe_timeout <= 0):
        raise SystemExit("debugger probing requires a reviewed executable hash and a positive probe timeout")
    options.out = options.out.resolve()
    options.out.mkdir(parents=True, exist_ok=False)
    if options.replay:
        saved = json.loads(options.replay.read_text())
        saved = saved if isinstance(saved, list) else [saved.get("case", saved)]
        cases = [Case.from_dict(value) for value in saved]
        if not cases or len({case.number for case in cases}) != len(cases):
            raise SystemExit("replay requires nonempty, uniquely numbered cases")
        options.families = sorted({case.family for case in cases})
    else:
        cases = generate(options.seed, options.per_family, options.families)
    save_json(options.out / "cases.json", [case.to_dict() for case in cases])
    if options.emit_lean:
        (options.out / "GeneratedScalarAdmission.lean").write_text(lean_controls(cases))
    summary = {"schema": 1, "seed": options.seed, "binary": str(binary), "binary_sha256": identity,
               "modes": options.modes, "stats": options.stats, "timeout": options.timeout,
               "output_limit": options.output_limit, "per_family": options.per_family,
               "qualified_requested": options.qualified, "source_files": {
                   name: sha256(Path(__file__).with_name(name)) for name in
                   ("check_native_observers.py", "native_observer_cases.py", "compare_answers.py",
                    "native_observer_probes.py", "native_observer_gdb.py")},
               "gdb_probe": options.gdb_probe, "probe_timeout": options.probe_timeout,
               "reference_flags": list(REFERENCE_FLAGS), "cases": [], "failures": [],
               "route_coverage": {family: 0 for family in options.families}, "metamorphic_pairs": [],
               "subroute_coverage": {family: dict.fromkeys(routes, 0) for family, routes in REQUIRED_SUBROUTES.items()
                                      if family in options.families},
               "storage_pairs": [], "coverage_cells": {}, "source_inventory": {}, "reference_controls": []}
    for family in options.families:
        filename, anchor = SOURCE_ANCHORS[family]
        path = ROOT / filename
        summary["source_inventory"][family] = {"file": filename, "anchor": anchor,
            "sha256": sha256(path), "anchor_present": anchor in path.read_text(),
            "boundary": "Working-source snapshot; binary/source correspondence requires its build receipt."}
    for value, expr in (("7", integer(7)), ("true", Expr("boolean", "test", token="true"))):
        for observer in OBSERVERS:
            directory = options.out / f"control-{value}-{observer}"
            directory.mkdir()
            case = Case(0, "scalar-fold", observer, ("2",), expr)
            record = evaluate_case(case, binary, "off", directory, options)
            oracle = Run("exited", 0, constant_oracle(observer, value), "", 0.0, {}, [])
            native_law = compare(Run(**record["native"]), oracle, record["contracts"])
            reference_law = compare(Run(**record["reference"]), oracle, record["contracts"])
            summary["reference_controls"].append({"value": value, "observer": observer,
                "native_law": native_law, "reference_law": reference_law})
            save_json(directory / "oracle.json", {"expected_stdout": oracle.stdout,
                "native_law": native_law, "reference_law": reference_law})
            if native_law != "equal" or reference_law != "equal":
                summary["failures"].append(directory.name)
    for case in cases:
        first_mode_record = None
        first_mode = None
        for mode in options.modes:
            directory = options.out / f"case-{case.number:04d}-{mode}"
            directory.mkdir()
            record = evaluate_case(case, binary, mode, directory, options)
            if first_mode_record is None:
                first_mode_record, first_mode = record, mode
            else:
                storage = {"case": case.number, "first_mode": first_mode, "second_mode": mode,
                    "native_law": compare(Run(**record["native"]), Run(**first_mode_record["native"]), record["contracts"]),
                    "reference_law": compare(Run(**record["reference"]), Run(**first_mode_record["reference"]), record["contracts"])}
                summary["storage_pairs"].append(storage)
                save_json(directory / "storage-comparison.json", storage)
                if storage["native_law"] != "equal" or storage["reference_law"] != "equal":
                    summary["failures"].append(str((directory / "storage-comparison.json").relative_to(options.out)))
            summary["cases"].append({"case": case.number, "family": case.family, "observer": case.observer,
                                     "mode": mode, "verdict": record["verdict"], "directory": directory.name})
            cell = summary["coverage_cells"].setdefault(case.family + "/" + case.observer, {})
            level = record["coverage"]["level"]
            cell[level] = cell.get(level, 0) + 1
            if level != "unconfirmed":
                summary["route_coverage"][case.family] += 1
                for lane in record["coverage"].get("subroutes", []):
                    if lane in summary["subroute_coverage"].get(case.family, {}):
                        summary["subroute_coverage"][case.family][lane] += 1
            if not options.no_metamorphic:
                variant_directory = directory / "irrelevant-definition"
                variant_directory.mkdir()
                variant = Case.from_dict({**case.to_dict(), "variant": "irrelevant-definition"})
                changed = evaluate_case(variant, binary, mode, variant_directory, options)
                contracts = record["contracts"]
                native_law = compare(Run(**changed["native"]), Run(**record["native"]), contracts)
                reference_law = compare(Run(**changed["reference"]), Run(**record["reference"]), contracts)
                summary["metamorphic_pairs"].append({"case": case.number, "mode": mode,
                    "transformation": "irrelevant-definition", "native_law": native_law,
                    "reference_law": reference_law, "variant_differential": changed["verdict"]})
                if any(verdict != "equal" for verdict in (native_law, reference_law, changed["verdict"])):
                    summary["failures"].append(str(variant_directory.relative_to(options.out)))
            if record["verdict"] != "equal":
                summary["failures"].append(directory.name)
                if options.shrink_budget and record["verdict"].endswith("mismatch"):
                    shrink_root = directory / "shrink"
                    shrink_root.mkdir()
                    attempt = 0

                    def evaluate(candidate):
                        nonlocal attempt
                        child = shrink_root / str(attempt)
                        child.mkdir()
                        attempt += 1
                        outcome = evaluate_case(candidate, binary, mode, child, options)
                        if outcome["verdict"] != record["verdict"]:
                            return None
                        if record["coverage"]["level"] != "unconfirmed" and outcome["coverage"]["level"] != record["coverage"]["level"]:
                            return None
                        return (candidate.family, candidate.observer, outcome["verdict"])

                    minimal, receipt = shrink(case, evaluate, options.shrink_budget)
                    save_json(shrink_root / "minimized.json", {"case": minimal.to_dict(), **receipt})
                    (shrink_root / "minimized.metta").write_text(minimal.render()[0])
            save_json(options.out / "summary.json", summary)
            print(f"{case.number:04d} {case.family} {case.observer} {mode}: {record['verdict']}", flush=True)
    summary["binary_unchanged"] = sha256(binary) == identity
    summary["harness_unchanged"] = all(sha256(Path(__file__).with_name(name)) == digest
                                       for name, digest in summary["source_files"].items())
    summary["unconfirmed_routes"] = [family for family, count in summary["route_coverage"].items() if not count]
    summary["unconfirmed_subroutes"] = [family + "/" + lane
        for family, lanes in summary["subroute_coverage"].items() for lane, count in lanes.items() if not count]
    coverage_incomplete = bool(summary["unconfirmed_routes"] or summary["unconfirmed_subroutes"])
    summary["status"] = ("invalid-binary-changed" if not summary["binary_unchanged"] else
                         "invalid-harness-changed" if not summary["harness_unchanged"] else
                         "differences-or-inconclusive" if summary["failures"] else
                         "observations-agree-coverage-incomplete" if coverage_incomplete else
                         "qualified" if options.qualified else "provisional-observations-agree")
    save_json(options.out / "summary.json", summary)
    print(summary["status"])
    return 1 if summary["failures"] or not summary["binary_unchanged"] or not summary["harness_unchanged"] else 2 if options.qualified and coverage_incomplete else 0


if __name__ == "__main__":
    raise SystemExit(main())
