#!/usr/bin/env python3
"""Optional GSLT -> PeTTa -> transform demonstration from recorded invocations.

    replay_gslt.py --corpus DIR --keep DIR [--report FILE]
                                  [--timeout SECONDS] [--turns STEPS COUNT]
                                  [--fire transform|direct] [--only NAME ...]
                                  [--jobs N]

DIR holds invocation manifests made by record_petta_direct_run.sh. Replays
preserve the recorded command/profile, cwd, environment, input order and file
origins. Generated inputs removed by a gate are restored from checked bytes;
live inputs, executables and library contexts must still have their recorded
identities. The machine receives one transformed document per input document.

Comparison uses out-of-band structural values and host-service effects, with
original queries mapped to their retained machine turns. Pending, failed,
unobserved and stale runs never qualify by agreeing with another failure.
"""
import argparse
import concurrent.futures as futures
from contextlib import ExitStack
import json
import os
from pathlib import Path
import signal
import tempfile
import threading
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests/support"))
import gslt_invocation as invocation
import native_observation

import compare_corpus as corpus

whole, forms, swi_lane = corpus.whole, corpus.forms, corpus.swi_lane
ROOT = forms.ROOT
GENERATED = os.path.join("langdef", "petta", "generated")
QUIET = "(= (pm:quiet $done) (empty))"


def with_compiled_programs(source, origin, imported):
    """Install definition-only generated imports in the guest machine too.

    Executable module imports keep the host's module protocol; they are not
    flattened into a document with a different declaration/effect order.
    """
    result, bang = [], False
    for form in swi_lane.read_forms(source):
        if form == "!":
            bang = True
            continue
        inline = None
        if bang and isinstance(form, list) and len(form) == 3 and form[:2] == ["import!", "&self"]:
            target = form[2]
            if isinstance(target, str) and not target.startswith(("$", '"', "r#")):
                for base in (os.path.dirname(origin), ROOT):
                    for suffix in ("", ".metta"):
                        path = os.path.realpath(os.path.join(base, target + suffix))
                        if os.path.isfile(path) and GENERATED in os.path.relpath(path, ROOT):
                            with open(path) as stream:
                                definitions = swi_lane.read_forms(stream.read())
                            if "!" not in definitions:
                                inline = [] if path in imported else definitions
                                imported.add(path)
                            break
                    if inline is not None:
                        break
        if inline is not None:
            result.extend(inline)
            result.extend(["!", ["quote", "true"]])
        else:
            if bang:
                result.append("!")
            result.append(form)
        bang = False
    return swi_lane.write_forms(result)


def through_the_machine(source, lane_import, initialize=True, plan=None):
    """SOURCE with its equations and declarations given to the machine and its
    queries asked of the machine."""
    if plan is None:
        plan = dict(queries=0, guests=[])
    lines = []

    def query(text):
        lines.append(text)
        plan["queries"] += 1

    if initialize:
        query(lane_import)
        query(corpus.MACHINE)
        lines.extend([corpus.ASK, QUIET])
        query("!(pm:hide-loaded!)")
    if initialize and corpus.FIRE == "direct":
        query("!(pm:quiet (pm:lane! direct))")
    read = swi_lane.read_forms(source)
    initial = initialize and bool(read) and all(
        corpus.is_equation(form) or corpus.is_arrow(form) for form in read)
    equations = []
    if initial:
        query("!(== (collapse (match &pm (pm:sources $name $items) found)) ())")
        plan.setdefault("registration_preconditions", []).append(plan["queries"])
    bang = False
    for form in read:
        if form == "!":
            bang = True
            continue
        if not bang and corpus.is_arrow(form):
            query("!(pm:quiet (pm:type-text %s))" % corpus.as_text(form))
        bang = False
    bang = False
    for form in read:
        if form == "!":
            bang = True
            continue
        if bang:
            begin = plan["queries"] + 1
            query("!(pm:begin-quietly %s)" % corpus.as_text(form))
            steps, count = corpus.TURNS if corpus.TURNS is not None else ("none", 1)
            for _ in range(count):
                query("!(pm:advance-quietly %s)" % steps)
            query("!(get-state &pm-outcome)")
            status = plan["queries"]
            query("!(let $answers (pm:end) (if (== (get-state &pm-outcome) pm:halted) (superpose $answers) (empty)))")
            plan["guests"].append(dict(begin=begin, end=plan["queries"], status=status))
        else:
            lines.append(swi_lane.write_form(form))
            if corpus.is_equation(form):
                if initial:
                    equations.append(corpus.as_text(form))
                else:
                    query("!(pm:quiet (pm:program-text %s))" % corpus.as_text(form))
            elif corpus.is_declaration(form):
                query("!(pm:quiet (pm:type-text %s))" % corpus.as_text(form))
        bang = False
    # With no executable form in the initial document, declare its names
    # before compiling bodies. Independent parses retain equation scopes;
    # later files and interleaved programs use the incremental path above.
    if initial:
        for phase in ("declare", "define"):
            for equation in equations:
                query("!(pm:quiet (pm:initial-%s-text %s))" % (phase, equation))
    return "\n".join(lines) + "\n"


def run_invocation(record, arguments, timeout, observation=None):
    observed = (["--run-observation", str(observation),
                 "--run-report", str(observation) + ".process.json"] if observation else [])
    return swi_lane.run_group(record["command"] + observed + arguments, record["cwd"], timeout * 6,
                              env=invocation.replay_environment(record), cpu_seconds=timeout,
                              text=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--keep", required=True, help="where the runs' files are written")
    parser.add_argument("--report")
    parser.add_argument("--timeout", type=int, default=900,
                        help="processor seconds for each run (the longest program of the "
                             "corpus fires a million rules in about 300)")
    parser.add_argument("--turns", nargs=2, type=int, metavar=("STEPS", "COUNT"),
                        default=[100000, 60],
                        help="drive each query in COUNT turns of STEPS rules")
    parser.add_argument("--fire", choices=["transform", "direct"], default="transform",
                        help="how the machine's rules are fired")
    parser.add_argument("--lane", choices=["native", "specs"], default="native",
                        help="production transform library or its MeTTa specification")
    parser.add_argument("--only", nargs="*")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2),
                        help="programs replayed side by side")
    args = parser.parse_args()
    corpus.FIRE = args.fire
    corpus.TURNS = tuple(args.turns)
    keep = os.path.abspath(args.keep)
    os.makedirs(keep, exist_ok=True)
    lane_import = swi_lane.base.LIBRARY_IMPORT if args.lane == "native" else swi_lane.base.SPECS_IMPORT
    files = sorted(name for name in os.listdir(args.corpus) if name.endswith(".invocation.json"))
    if args.only:
        files = [name for name in files if any(name.startswith(prefix) for prefix in args.only)]
    if not files:
        print(f"FAIL: no invocation records in {args.corpus}")
        return 1
    harness = [*Path(__file__).parent.glob("*.py"),
               *(invocation.ROOT / "tests/support").glob("*.py")]
    implementation = invocation.digest(json.dumps([
        (path.name, invocation.digest(path.read_bytes()))
        for path in sorted(harness)]).encode())
    printing = threading.Lock()

    def say(text):
        with printing:
            print(text, flush=True)

    def qualify(name, record):
        """One program's row: its runs as written and through the machine,
        compared.  RECORD is its restored record, or the error refusing it."""
        stem = name[:-len(".invocation.json")]
        origin = os.path.abspath(os.path.join(args.corpus, name))
        checkpoint = Path(keep) / (stem + ".qualification.json")
        fingerprint = invocation.digest(json.dumps([
            invocation.digest(Path(origin).read_bytes()), implementation,
            args.fire, args.lane, args.turns, args.timeout]).encode())
        try:
            if isinstance(record, Exception):
                raise record
            with ExitStack() as cleanup:
                invocation.require_completed(record)
                if not record["sources"]:
                    raise ValueError("invocation has no recorded source documents")
                if checkpoint.exists():
                    prior = json.loads(checkpoint.read_text())
                    if prior.get("fingerprint") == fingerprint and prior["row"][1] == "same-native-observation":
                        say(f"REUSE: {stem}")
                        return tuple(prior["row"])
                written_events = Path(keep) / (stem + ".written.events.jsonl")
                machine_events = Path(keep) / (stem + ".machine.events.jsonl")
                code, out, err = run_invocation(record, record["arguments"], args.timeout, written_events)
                machine_arguments = list(record["arguments"])
                imported = set()
                plan = dict(queries=0, guests=[])
                for index, item in enumerate(record["sources"]):
                    source_path = item["path"]
                    with open(source_path) as stream:
                        source = with_compiled_programs(stream.read(), source_path, imported)
                    transformed = through_the_machine(source, lane_import, initialize=index == 0, plan=plan)
                    with tempfile.NamedTemporaryFile(mode="w", prefix=".gslt-machine-", suffix=".metta",
                            dir=os.path.dirname(source_path), delete=False) as stream:
                        stream.write(transformed)
                        machine_path = stream.name
                    cleanup.callback(os.unlink, machine_path)
                    machine_arguments[item["argument"]] = machine_path
                    with open(os.path.join(keep, f"{stem}.{index}.machine.metta"), "w") as stream:
                        stream.write(transformed)
                invocation.save_json(Path(keep) / (stem + ".query-map.json"), plan)
                origins = []
                for index, item in enumerate(record["sources"]):
                    origins += ["--source-origin", str(index), record["arguments"][item["argument"]]]
                mcode, mout, merr = run_invocation(record, origins + machine_arguments, args.timeout, machine_events)
        except (OSError, ValueError) as error:
            say(f"unqualified-invocation {stem}: {error}")
            return (stem, "unqualified-invocation", 0, str(error))
        for suffix, text in (("written.out", out), ("written.err", err),
                             ("machine.out", mout), ("machine.err", merr)):
            with open(os.path.join(keep, f"{stem}.{suffix}"), "wb") as stream:
                stream.write(text)
        held, unended, observation_error = False, mcode == 124, None
        a = b = None
        if code == 0 and mcode == 0:
            try:
                host = native_observation.read_observation(written_events)
                machine = native_observation.read_observation(machine_events)
                for number in plan.get("registration_preconditions", []):
                    if machine["queries"][number]["answers"] != [[["symbol", "true".encode().hex()]]]:
                        raise ValueError("initial program registration did not start empty")
                a = native_observation.project(host, [(i, i) for i in host["queries"]])
                b = native_observation.project(machine, plan["guests"])
                for span in plan["guests"]:
                    status = machine["queries"][span["status"]]["answers"]
                    if status == [[["symbol", "pm:halted".encode().hex()]]]:
                        continue
                    if status in ([[["symbol", name.encode().hex()]]]
                                  for name in ("pm:turn-over", "pm:out-of-fuel")):
                        unended = True
                    else:
                        held = True
                if host["final"]["contract_exit"] or machine["final"]["contract_exit"]:
                    observation_error = "native query contract did not complete successfully"
            except (OSError, ValueError, KeyError) as error:
                observation_error = str(error)
        host_signal = native_observation.process_signal(code, written_events)
        machine_signal = native_observation.process_signal(mcode, machine_events)
        if code == 124:
            verdict = "host-not-ended"
        elif host_signal is not None:
            verdict = "host-resource-pending" if host_signal == signal.SIGXCPU else "host-interrupted"
        elif machine_signal is not None:
            verdict = "machine-resource-pending" if machine_signal == signal.SIGXCPU else "machine-interrupted"
        elif unended:
            verdict = "not-ended"
        elif code != 0:
            verdict = "host-failed"
        elif mcode != 0 or held:
            verdict = "machine-failed"
        elif observation_error is not None:
            verdict = "unqualified-observation"
        elif a == b:
            verdict = "same-native-observation"
        else:
            verdict = "DIFFER"
        note = ""
        if host_signal is not None or machine_signal is not None:
            note = f"physical signal {host_signal or machine_signal}; no completed query frontier"
        if observation_error:
            note = observation_error
        if verdict in ("DIFFER", "not-ended"):
            first = lambda data: next((line for line in data.splitlines() if line.strip()), b"")[:110]
            note = (f"written exit {code}, {len(out)} display bytes {first(err)!r} | "
                    f"machine exit {mcode}, {len(mout)} display bytes {first(merr)!r}")
        count = sum(len(query["answers"]) for query in a) if a is not None else 0
        row = (stem, verdict, count, note)
        invocation.save_json(checkpoint, dict(fingerprint=fingerprint, row=row))
        say(f"{verdict:14s} {stem:62s} {count:4d} answers  {note}")
        return row

    # Programs run side by side, batch by batch.  A batch restores the
    # generated inputs its records lack under the context lock, which it holds
    # until its runs end and those inputs are removed; its records agree on
    # every input they declare (invocation.replay_batches).
    rows = []
    with futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for batch in invocation.replay_batches(
                [os.path.join(args.corpus, name) for name in files]):
            names = [os.path.basename(path) for path in batch]
            with invocation.restored_batch(batch) as records:
                rows.extend(pool.map(qualify, names, records))
    rows.sort(key=lambda row: row[0])
    by = {}
    for row in rows:
        by[row[1]] = by.get(row[1], 0) + 1
    summary = (f"programs {len(rows)}: " + ", ".join(f"{kind} {count}" for kind, count in sorted(by.items()))
               + f" (rules fired by {'the host' if args.fire == 'direct' else args.lane + ' transform'}, "
               f"turns of {args.turns[0]} x {args.turns[1]}, {args.timeout} s)")
    if args.report:
        with open(args.report, "w") as stream:
            for stem, verdict, count, note in rows:
                stream.write(f"{verdict:14s} {stem:62s} {count:4d} answers  {note}\n")
            stream.write(summary + "\n")
    failed = [row for row in rows if row[1] != "same-native-observation"]
    if failed:
        print("FAIL: " + summary)
        return 1
    print("PASS: recorded query frontiers agree in structural values and host-service effects: " + summary)
    print("Yield timing, uninstrumented foreign effects and values without a codec remain separate obligations.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
