"""The complete suite, one stage at a time, cheapest first.

The stages are the Makefile's: test-all's own gates check correctness, then
come test-asymptotic, test-examples and test-comprehensive.  Within a stage,
the gates that failed when last run go first, then the longest.  One make
first brings the files of the stage's configuration up to date, and those of
its runtime-stats build, so no two makes build the same file.  The gates that
build another configuration then run one after another in one lane; the
others run beside them, --jobs at a time in all, each in its own make.  The
asymptotic checks run one at a time, as other runs would disturb what they
measure.  A gate that is a prerequisite of another gate of its stage runs
inside that gate; a gate that only gathers others runs as those gates.  The
run stops after the first stage with a failure.

Each gate's latest output is runtime/main-readiness/suite/logs/GATE.log, and
every result is appended to runtime/main-readiness/suite/results.tsv.
"""

from __future__ import annotations

import collections
import concurrent.futures
import fcntl
import re
import subprocess
import sys
import time
from pathlib import Path

STAGES = ("correctness", "asymptotic", "examples", "comprehensive")
STAGE_TARGETS = {f"test-{stage}" for stage in STAGES[1:]}


def make_database(root, settings=()):
    """Prerequisites, recipes and phony targets, as make itself reads them.

    The goal has no rule, so make prints what it read and runs nothing, not even
    the recipe lines that -n runs; its name starts with test, as test-all's
    does, so the Makefile reads the configuration test-all gets."""
    text = subprocess.run(["make", "-pq", *settings, "test-suite-readiness-database"],
                          cwd=root, capture_output=True, text=True).stdout
    prerequisites = collections.defaultdict(list)
    recipes = collections.defaultdict(str)
    phony, ordered = set(), set()
    current = None
    for line in text.splitlines():
        if line.startswith("\t"):
            if current:
                recipes[current] += line + "\n"
        elif line.startswith("#"):
            if current and "Phony target" in line:
                phony.add(current)
        elif not line.strip():
            current = None
        else:
            rule = re.match(r"^([^\s:#=]+):(?![:=])\s*(.*)$", line)
            if rule and not re.match(r"^\S+\s*[:+?]?=", rule.group(2)):
                current = rule.group(1)
                names = rule.group(2).replace("|", " ").split()
                if ".WAIT" in names:
                    ordered.add(current)
                prerequisites[current].extend(name for name in names if name != ".WAIT")
    return prerequisites, recipes, phony, ordered


def expand_aggregates(gates, database):
    """A gate that only gathers other gates, with no recipe of its own and no
    order among them, runs as those gates, so they spread over the jobs."""
    prerequisites, recipes, phony, ordered = database
    serial = set(prerequisites.get(".NOTPARALLEL", ())) | ordered
    expanded = []

    def add(gate):
        parts = prerequisites.get(gate, ())
        if (parts and not recipes.get(gate) and gate not in serial
                and all(part.startswith(("test-", "check-")) for part in parts)):
            for part in dict.fromkeys(parts):
                add(part)
        elif gate not in expanded:
            expanded.append(gate)

    for gate in gates:
        add(gate)
    return expanded


STATS_SETTINGS = ("ENABLE_RUNTIME_STATS=1",)


def stats_goals(line, target):
    """The goals of a recipe line that only runs make again in the runtime-stats
    build of this configuration, or None for any other line that runs make."""
    if "$(MAKE)" not in line or line.rstrip().endswith("\\"):
        return None
    goals = []
    for command in line.split("$(MAKE)")[1:]:
        # the make command ends where its shell command does
        words = re.split(r";|&&|\|\|?", command, maxsplit=1)[0].split()
        settings = [word for word in words if "=" in word]
        named = [word.replace("$@", target) for word in words
                 if "=" not in word and not word.startswith("-")]
        if (sorted(settings) not in (["ENABLE_RUNTIME_STATS=1"],
                                     ["BUILD=$(BUILD_CANON)", "ENABLE_RUNTIME_STATS=1"])
                or not named or any("$" in goal for goal in named)):
            return None
        goals += named
    return goals


def file_prerequisites(database, targets):
    """The files make needs before the recipes of these targets can run."""
    prerequisites, _, phony, _ = database
    return {name for target in targets
            for reached in closure(prerequisites, target) | {target}
            if is_gate(reached, phony)
            for name in prerequisites.get(reached, ()) if not is_gate(name, phony)}


def is_gate(name, phony):
    """A target that is a check rather than a file: declared phony, or named as
    a test or a check (some gates are not declared phony)."""
    return name in phony or name.startswith(("test-", "check-"))


def closure(prerequisites, target):
    seen, pending = set(), [target]
    while pending:
        for name in prerequisites.get(pending.pop(), ()):
            if name not in seen:
                seen.add(name)
                pending.append(name)
    return seen


def stage_gates(database):
    """Each stage's gates, in the Makefile's order.  A gate belongs to one stage."""
    prerequisites, _, phony, _ = database
    stages, owner = {}, {}
    for stage in STAGES:
        source = "test-all" if stage == "correctness" else f"test-{stage}"
        gates = [name for name in dict.fromkeys(prerequisites.get(source, ()))
                 if is_gate(name, phony) and name not in STAGE_TARGETS]
        for gate in gates:
            if gate in owner:
                raise SystemExit(f"FAIL: {gate} is in both {owner[gate]} and {stage}")
            owner[gate] = stage
        stages[stage] = gates
    return stages


def last_results(path):
    """Each gate's latest result and time."""
    results = {}
    if path.exists():
        for line in path.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) >= 5:
                results[fields[1]] = (fields[3], float(fields[4]))
    return results


def run_gate(root, state, stage, gate, jobs):
    log_path = state / "logs" / f"{gate}.log"
    started = time.monotonic()
    with log_path.open("w") as log:
        status = subprocess.run(["make", "-s", f"-j{jobs}", gate], cwd=root, stdout=log,
                                stderr=subprocess.STDOUT).returncode
    seconds = time.monotonic() - started
    result = "pass" if status == 0 else "fail"
    with (state / "results.tsv").open("a") as results:
        results.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')}\t{gate}\t{stage}\t{result}\t"
                      f"{seconds:.1f}\n")
    print(f"{result:4s} {seconds:7.1f}s  {gate}", flush=True)
    return gate, result


def made_goals(database, target):
    """The goals the recipes under a target run make for in the runtime-stats
    build, or None when one runs make for anything else."""
    prerequisites, recipes, _, _ = database
    goals = []
    for name in closure(prerequisites, target) | {target}:
        # a recipe line continued with a backslash is one command
        for line in recipes.get(name, "").replace("\\\n", " ").splitlines():
            if "$(MAKE)" in line:
                found = stats_goals(line, name)
                if found is None:
                    return None
                goals += found
    return goals


def stats_runs(database, stats_database, gate):
    """The runtime-stats goals a gate makes, followed into the runtime-stats
    build itself, or None when anything on the way makes something else."""
    pending = made_goals(database, gate)
    if pending is None:
        return None
    goals = set()
    while pending:
        goal = pending.pop()
        if goal not in goals:
            goals.add(goal)
            more = made_goals(stats_database(), goal)
            if more is None:
                return None
            pending += more
    return sorted(goals)


def run_stage(root, state, stage, gates, database, stats_database, jobs, last, dry_run):
    prerequisites = database[0]
    if stage == "asymptotic":
        jobs = 1  # growth is measured in time and memory, which other runs disturb
    inside = set()
    for gate in gates:
        inside |= closure(prerequisites, gate)
    gates = [gate for gate in gates if gate not in inside]
    # last time's failures first, then the longest, so none starts last
    gates.sort(key=lambda gate: (last.get(gate, ("", 0.0))[0] != "fail",
                                 -last.get(gate, ("", 0.0))[1]))
    # A gate that makes another configuration runs alone.  A gate that only
    # makes its runtime-stats build runs with the others, once that build's
    # files are made: a make whose files are up to date only reads them.
    stats = {gate: stats_runs(database, stats_database, gate) for gate in gates}
    building = [gate for gate in gates if stats[gate] is None]
    others = [gate for gate in gates if stats[gate] is not None]
    files = sorted(file_prerequisites(database, others))
    stats_targets = sorted({goal for gate in others for goal in stats[gate]})
    stats_files = (sorted(file_prerequisites(stats_database(), stats_targets))
                   if stats_targets else [])
    print(f"STAGE {stage}: {len(gates)} gates, {len(building)} building other "
          f"configurations, {len(others)} at {jobs} at a time", flush=True)
    if dry_run:
        for gate in building + others:
            print(f"  {gate}")
        return []
    started = time.monotonic()
    for settings, needed, name in (((), files, "files"),
                                   (STATS_SETTINGS, stats_files, "stats-files")):
        if needed:
            with (state / "logs" / f"{stage}-{name}.log").open("w") as log:
                subprocess.run(["make", "-s", "-k", f"-j{jobs}", "CETTA_TEST_ISOLATED=1",
                                *settings, *needed], cwd=root, stdout=log,
                               stderr=subprocess.STDOUT)
    # The gates that build other configurations go one after another in one
    # lane, beside the others: the files those use are already made.
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        lane = pool.submit(lambda: [run_gate(root, state, stage, gate, max(1, jobs - 1))
                                    for gate in building])
        rest = list(pool.map(lambda gate: run_gate(root, state, stage, gate, 1), others))
        outcomes = lane.result() + rest
    failed = [gate for gate, result in outcomes if result == "fail"]
    print(f"STAGE {stage} {'FAIL' if failed else 'PASS'}: {len(outcomes)} gates, "
          f"{len(failed)} failed, {time.monotonic() - started:.0f}s"
          + (": " + " ".join(failed) if failed else ""), flush=True)
    return failed


def run_suite(args, root):
    jobs = args.jobs if args.jobs is not None else 3
    if jobs < 1:
        print("FAIL: --jobs must be positive", file=sys.stderr)
        return 2
    state = root / "runtime/main-readiness/suite"
    (state / "logs").mkdir(parents=True, exist_ok=True)
    if args.dry_run:
        return run_stages(args, root, state, jobs)
    with (state / "runner.lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("FAIL: a suite run is already running in this checkout", file=sys.stderr)
            return 2
        return run_stages(args, root, state, jobs)


def run_stages(args, root, state, jobs):
    database = make_database(root)
    stats_cache = []

    def stats_database():
        if not stats_cache:
            stats_cache.append(make_database(root, STATS_SETTINGS))
        return stats_cache[0]
    stages = stage_gates(database)
    previous = last_results(state / "results.tsv")
    chosen = [args.stage] if args.stage else list(STAGES)
    started = time.monotonic()
    for stage in chosen:
        gates = expand_aggregates(stages[stage], database)
        if args.target:
            wanted = set(args.target).union(
                *(closure(database[0], name) for name in args.target))
            gates = [gate for gate in gates if gate in wanted]
        if args.failed:
            gates = [gate for gate in gates if previous.get(gate, ("",))[0] == "fail"]
        if gates and run_stage(root, state, stage, gates, database, stats_database, jobs,
                               previous, args.dry_run):
            print(f"SUITE FAIL at the {stage} stage, {time.monotonic() - started:.0f}s; "
                  f"logs in {(state / 'logs').relative_to(root)}")
            return 1
    if not args.dry_run:
        print(f"SUITE PASS: {', '.join(chosen)}, {time.monotonic() - started:.0f}s")
    return 0
