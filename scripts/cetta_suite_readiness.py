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
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

STAGES = ("correctness", "asymptotic", "examples", "comprehensive")
STAGE_TARGETS = {f"test-{stage}" for stage in STAGES[1:]}


def make_database(root, settings=(), environment=None):
    """Prerequisites, recipes and phony targets, as make itself reads them.

    The goal has no rule, so make prints what it read and runs nothing, not even
    the recipe lines that -n runs; its name starts with test, as test-all's
    does, so the Makefile reads the configuration test-all gets."""
    text = subprocess.run(["make", "-pq", *settings, "test-suite-readiness-database"],
                          cwd=root, capture_output=True, text=True,
                          env=environment).stdout
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
    """Other configurations launched by check recipes, after preparation.

    Recursive recipes that only produce files (such as a native archive)
    are already covered by file_prerequisites; they do not serialize every
    check that needs the runtime.
    """
    prerequisites, recipes, phony, _ = database
    goals = []
    for name in closure(prerequisites, target) | {target}:
        if not is_gate(name, phony):
            continue
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


def make_environment(exported):
    """The environment of a make a parent make starts: ours, with what the
    parent exports."""
    environment = dict(os.environ)
    environment.update(exported)
    return environment


def configuration_arguments(configuration):
    """A configuration is a make's command-line settings, its goals and what
    its parent exports.  The goals are given as MAKECMDGOALS, since the
    Makefile reads some settings from them."""
    settings, goals, _ = configuration
    return [*settings, f"MAKECMDGOALS={' '.join(goals)}"]


def configuration_database(root, configuration, cache):
    if configuration not in cache:
        cache[configuration] = make_database(
            root, configuration_arguments(configuration),
            make_environment(dict(configuration[2])))
    return cache[configuration]


def evaluate(root, configuration, text, cache):
    """TEXT as make expands it in this configuration and the shell then
    prints it, or None when make cannot."""
    key = (configuration, text)
    if key not in cache:
        marker = "@@suite-readiness@@"
        made = subprocess.run(
            ["make", "--no-print-directory", "-s", *configuration_arguments(configuration),
             f"--eval=suite-readiness-evaluate: ; @printf '%s\\n' '{marker}' {text}",
             "suite-readiness-evaluate"],
            cwd=root, capture_output=True, text=True,
            env=make_environment(dict(configuration[2])))
        printed = made.stdout.split(marker + "\n", 1)
        cache[key] = printed[1] if made.returncode == 0 and len(printed) == 2 else None
    return cache[key]


def configuration_identity(root, configuration, cache):
    """What a configuration builds: its object tag, whether it counts runtime
    statistics, and the path of its binary."""
    printed = evaluate(root, configuration,
                       "'$(BUILD_OBJ_TAG)|$(ENABLE_RUNTIME_STATS)|$(abspath $(BIN))'",
                       cache)
    fields = printed.split() if printed else []
    if len(fields) != 1 or fields[0].count("|") != 2:
        return None
    return tuple(fields[0].split("|"))


# A make started with these flags makes what an ordinary make would; any other
# flag (-B, -C, -f, -e, -t, -q, -i, ...) changes what it makes or where.
PLAIN_MAKE_FLAGS = {"-s", "--silent", "--quiet", "-k", "--keep-going",
                    "--no-print-directory"}


def make_words(command, gate):
    """The settings and goals of one make a recipe line starts, its make
    references left for the caller; "dry" when it makes nothing (-n), None
    when a flag changes what it makes."""
    try:
        # the shell removes the quotes of the words it passes to make
        words = shlex.split(re.split(r";|&&|\|\|?", command, maxsplit=1)[0])
    except ValueError:
        return None
    # the make ends where its output is redirected
    for index, word in enumerate(words):
        if re.match(r"^(\d*[<>]|&>)", word):
            words = words[:index]
            break
    flags = [word for word in words if word.startswith("-")]
    if any(word in ("--dry-run", "--just-print", "--recon") or
           (not word.startswith("--") and "n" in word[1:]) for word in flags):
        return "dry"
    for word in flags:
        if not (word in PLAIN_MAKE_FLAGS or re.fullmatch(r"-j\d*|--jobs(=\d+)?", word)
                or (not word.startswith("--") and set(word[1:]) <= set("sk"))):
            return None
    settings, goals = [], []
    for word in words:
        if not word.startswith("-"):
            (settings if "=" in word else goals).append(word.replace("$@", gate))
    return settings, goals


def expand_words(root, configuration, words, cache):
    """WORDS with their make references expanded in the configuration of the
    make whose recipe holds them; an escaped $$ is left as one $ for the make
    they start.  None when a word holds both."""
    pending = [word for word in words if "$" in word.replace("$$", "")]
    if any("$$" in word for word in pending):
        return None
    values = {}
    if pending:
        separator = "@@word@@"
        printed = evaluate(root, configuration,
                           f" {separator} ".join(f"'{word}'" for word in pending), cache)
        if printed is None:
            return None
        groups = " ".join(printed.split()).split(separator)
        if len(groups) != len(pending):
            return None
        values = {word: group.split() for word, group in zip(pending, groups)}
    expanded = []
    for word in words:
        expanded += values[word] if word in values else [word.replace("$$", "$")]
    return expanded


def configuration_runs(root, database, target, parent, identity, caches, depth=0):
    """The makes a gate starts in other configurations, followed into them:
    a list of (configuration, identity), or None when a recipe starts make in
    a way its words do not name, or nests too deep."""
    if depth > 4:
        return None
    prerequisites, recipes, phony, _ = database
    inherited = dict(item.split("=", 1) for item in parent[0])
    # what the Makefile exports to the makes its recipes start
    printed = evaluate(root, parent, "'$(BUILD)|$(CETTA_TEST_ISOLATED)'", caches["evaluate"])
    fields = printed.strip().split("|") if printed is not None else []
    if len(fields) != 2:
        return None
    exported = {"BUILD": fields[0], "CETTA_TEST_ISOLATED": fields[1],
                "CETTA_PARENT_BUILD_SIGNATURE": f"{identity[0]}:{identity[1]}",
                "CETTA_PARENT_BINARY": identity[2]}
    runs = []
    for name in sorted(closure(prerequisites, target) | {target}):
        if not is_gate(name, phony):
            continue
        for line in recipes.get(name, "").replace("\\\n", " ").splitlines():
            if "$(MAKE)" not in line:
                continue
            for command in line.split("$(MAKE)")[1:]:
                found = make_words(command, name)
                if found == "dry":
                    continue
                if found is None:
                    return None
                own, goals = found
                words = expand_words(root, parent, own + goals, caches["evaluate"])
                if words is None:
                    return None
                own = [word for word in words if "=" in word]
                goals = [word for word in words if "=" not in word]
                if not goals or any("$" in goal for goal in goals):
                    return None
                settings = dict(inherited)
                settings.update(item.split("=", 1) for item in own)
                child = (tuple(sorted(f"{key}={value}" for key, value in settings.items())),
                         tuple(goals), tuple(sorted(exported.items())))
                child_identity = configuration_identity(root, child, caches["evaluate"])
                if child_identity is None:
                    return None
                runs.append((child, child_identity))
                # an overridden binary is what the child passes on
                below = child
                if "BIN" in settings and os.path.abspath(
                        os.path.join(root, settings["BIN"])) != child_identity[2]:
                    settings["BIN"] = os.path.relpath(child_identity[2], root)
                    below = (tuple(sorted(f"{key}={value}" for key, value in settings.items())),
                             child[1], child[2])
                child_database = configuration_database(root, below, caches["database"])
                for goal in goals:
                    nested = configuration_runs(root, child_database, goal, below,
                                                child_identity, caches, depth + 1)
                    if nested is None:
                        return None
                    runs += nested
    return runs


def plan_key(root, gates, state):
    """What a plan depends on: the gates, the makefiles make reads, the
    configuration's tag (which names the toolchain's settings) and the files
    the tree holds (which the makefiles' wildcards read)."""
    printed = subprocess.run(
        ["make", "--no-print-directory", "-s",
         "--eval=suite-readiness-makefiles: ; @echo '$(BUILD_OBJ_TAG)' $(MAKEFILE_LIST)",
         "suite-readiness-makefiles"],
        cwd=root, capture_output=True, text=True).stdout.split()
    listed = subprocess.run(["git", "ls-files", "-co", "--exclude-standard"],
                            cwd=root, capture_output=True, text=True).stdout.splitlines()
    # the suite's own records are not the tree's files
    own = str(Path(state).resolve().relative_to(Path(root).resolve())) + "/"
    listed = [name for name in listed if not name.startswith(own)]
    digest = hashlib.sha256("\n".join(gates + listed).encode())
    # and the planner itself
    digest.update(Path(__file__).read_bytes())
    for word in printed:
        digest.update(word.encode())
        path = Path(root) / word
        if path.is_file():
            digest.update(path.read_bytes())
    return digest.hexdigest()


def plan_configurations(root, database, gates, jobs, state=None):
    """Which of the gates that make other configurations can run beside the
    others once those configurations are made, and what to make for them:
    the configurations, grouped by the objects they compile (tag and runtime
    statistics), each with the files its makes need.  A plan is kept in the
    state directory and reused while what it depends on is unchanged."""
    if not gates:
        return [], {}
    saved = state / "configuration-plan.json" if state is not None else None
    key = plan_key(root, gates, state) if saved is not None else None
    if saved is not None and saved.exists():
        try:
            kept = json.loads(saved.read_text())
            if kept.get("key") == key:
                groups = {tuple(objects): [
                    ((tuple(settings), tuple(goals), tuple(map(tuple, exported))), needed)
                    for (settings, goals, exported), needed in builds]
                    for objects, builds in kept["groups"]}
                return kept["movable"], groups
        except (OSError, ValueError, KeyError, TypeError):
            pass
    movable, groups = plan_configurations_afresh(root, database, gates, jobs)
    if saved is not None:
        saved.write_text(json.dumps({"key": key, "movable": movable, "groups": [
            [list(objects), [[[list(c[0]), list(c[1]), [list(pair) for pair in c[2]]], needed]
                             for c, needed in builds]]
            for objects, builds in groups.items()]}))
    return movable, groups


def plan_configurations_afresh(root, database, gates, jobs):
    caches = {"database": {}, "evaluate": {}}

    def runs_of(gate):
        parent = ((), (gate,), ())
        identity = configuration_identity(root, parent, caches["evaluate"])
        if identity is None:
            return gate, None
        return gate, configuration_runs(root, database, gate, parent, identity, caches)

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        found = list(pool.map(runs_of, gates))
    movable = []
    groups = collections.defaultdict(dict)
    for gate, runs in found:
        if runs is None:
            continue
        movable.append(gate)
        for configuration, identity in runs:
            _, _, phony, _ = child_database = configuration_database(
                root, configuration, caches["database"])
            goals = configuration[1]
            needed = file_prerequisites(child_database, goals) | {
                goal for goal in goals if not is_gate(goal, phony)}
            # makes of one configuration differing only in goals make one build
            key = (configuration[0], configuration[2], identity)
            entry = groups[identity[:2]].setdefault(key, [configuration, set()])
            entry[1] |= needed
    return movable, {objects: [(configuration, sorted(needed))
                               for configuration, needed in builds.values()]
                     for objects, builds in groups.items()}


def build_configurations(root, state, stage, groups, jobs):
    """Make each configuration's files: files more than one object set needs
    first, then the object sets side by side, the builds of one object set one
    after another.  Returns what failed."""
    owners = collections.defaultdict(set)
    for objects, builds in groups.items():
        for _, needed in builds:
            for name in needed:
                owners[name].add(objects)
    shared = {name for name, objects in owners.items() if len(objects) > 1}
    cpus = os.cpu_count() or 2
    width = max(1, min(jobs, len(groups)))

    def make(index, configuration, files, parallel):
        started = time.monotonic()
        build_log = state / "logs" / f"{stage}-build-{index}.log"
        with build_log.open("w") as log:
            made = subprocess.run(
                ["make", "-s", "-k", f"-j{parallel}", *configuration_arguments(configuration),
                 *files], cwd=root, stdout=log, stderr=subprocess.STDOUT,
                env=make_environment(dict(configuration[2])))
        return made.returncode, time.monotonic() - started, build_log

    failed = []
    index = 0
    for objects, builds in sorted(groups.items()):
        for configuration, needed in builds:
            files = [name for name in needed if name in shared]
            if files:
                index += 1
                status, seconds, log = make(index, configuration, files, cpus)
                shared -= set(files)
                if status != 0:
                    failed.append(f"{stage}-build-{index}")
                print(f"BUILT shared files for {objects[0]}{'-runtime-stats' if objects[1] == '1' else ''}"
                      f" in {seconds:.1f}s" + (" (FAILED)" if status else ""), flush=True)
    if failed:
        return failed
    work = []
    for objects, builds in sorted(groups.items()):
        steps = []
        for configuration, needed in builds:
            files = [name for name in needed if len(owners[name]) == 1]
            if files:
                index += 1
                steps.append((index, configuration, files))
        if steps:
            work.append((objects, steps))

    def build_objects(item):
        objects, steps = item
        outcome = []
        for step_index, configuration, files in steps:
            status, seconds, log = make(step_index, configuration, files,
                                        max(1, cpus // width))
            name = objects[0] + ("-runtime-stats" if objects[1] == "1" else "")
            print(f"BUILT {name} ({len(files)} files) in {seconds:.1f}s"
                  + (f" FAILED: see {log.relative_to(root)}" if status else ""), flush=True)
            if status != 0:
                outcome.append(f"{stage}-build-{step_index}")
        return outcome

    with concurrent.futures.ThreadPoolExecutor(max_workers=width) as pool:
        for outcome in pool.map(build_objects, work):
            failed += outcome
    return failed


def rebuilt_since(root, stamp, groups):
    """Objects and binaries of the prepared object sets that changed after
    STAMP: a make during the checks built what the plan did not."""
    tags = {objects[0] for objects in groups}
    found = subprocess.run(
        ["find", ".", "-path", "./.git", "-prune", "-o", "-type", "f", "-newer", str(stamp),
         "(", "-name", "*.o", "-o", "-path", "./runtime/cetta-*", ")", "-print"],
        cwd=root, capture_output=True, text=True).stdout.split()
    rebuilt = []
    for path in found:
        # a gate's own work directory under runtime/ holds what it makes
        parts = path.split("/")
        if (len(parts) > 3 and parts[1] == "runtime" and
                parts[2] not in ("objects", "bootstrap")):
            continue
        name = os.path.basename(path)
        if name.endswith(".o"):
            stem = name[:-2].removesuffix(".runtime-stats")
            if any(stem.endswith("." + tag) for tag in tags):
                rebuilt.append(path)
        elif name.startswith("cetta-") and name[6:].removesuffix("-runtime-stats") in tags:
            rebuilt.append(path)
    return sorted(rebuilt)


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
    # A gate that makes other configurations, each named by its recipes, runs
    # with the others too, once those configurations are made, each once.
    movable, groups = plan_configurations(root, database, building, jobs, state)
    building = [gate for gate in building if gate not in movable]
    others = [gate for gate in gates if gate not in building]
    files = sorted(file_prerequisites(database, others))
    stats_targets = sorted({goal for gate in others for goal in stats[gate] or ()})
    stats_files = (sorted(file_prerequisites(stats_database(), stats_targets))
                   if stats_targets else [])
    print(f"STAGE {stage}: {len(gates)} gates, {len(building)} building other "
          f"configurations, {len(others)} at {jobs} at a time; {len(movable)} of "
          f"those with the others, after {sum(map(len, groups.values()))} builds "
          f"of {len(groups)} object sets", flush=True)
    if dry_run:
        for objects, builds in sorted(groups.items()):
            for configuration, needed in builds:
                print(f"  build {objects[0]}{'-runtime-stats' if objects[1] == '1' else ''}: "
                      f"{len(needed)} files for {' '.join(configuration[1])[:60]}")
        for gate in building + others:
            print(f"  {gate}")
        return []
    started = time.monotonic()
    for settings, needed, name in (((), files, "files"),
                                   (STATS_SETTINGS, stats_files, "stats-files")):
        if needed:
            build_log = state / "logs" / f"{stage}-{name}.log"
            with build_log.open("w") as log:
                prepared = subprocess.run(
                    ["make", "-s", "-k", f"-j{jobs}", "CETTA_TEST_ISOLATED=1",
                     *settings, *needed], cwd=root, stdout=log,
                    stderr=subprocess.STDOUT)
            if prepared.returncode != 0:
                print(f"FAIL preparing {stage} {name}: no gates run; "
                      f"see {build_log.relative_to(root)}", flush=True)
                return [f"{stage}-{name}"]
    unbuilt = build_configurations(root, state, stage, groups, jobs)
    if unbuilt:
        print(f"FAIL preparing {stage} configurations: no gates run: "
              f"{' '.join(unbuilt)}; see the logs", flush=True)
        return unbuilt
    stamp = state / f"{stage}-prepared.stamp"
    stamp.write_text(time.strftime("%Y-%m-%d %H:%M:%S\n"))
    checks_started = time.monotonic()
    if files or stats_files or groups:
        print(f"PREPARED {stage}: {checks_started - started:.1f}s", flush=True)
    # The gates that build other configurations go one after another in one
    # lane, beside the others: the files those use are already made.
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        lane = pool.submit(lambda: [run_gate(root, state, stage, gate, max(1, jobs - 1))
                                    for gate in building])
        rest = list(pool.map(lambda gate: run_gate(root, state, stage, gate, 1), others))
        outcomes = lane.result() + rest
    failed = [gate for gate, result in outcomes if result == "fail"]
    rebuilt = rebuilt_since(root, stamp, groups)
    if rebuilt:
        print(f"WARN {len(rebuilt)} prepared objects or binaries were made again "
              f"during the checks, which the plan should have made first: "
              f"{' '.join(rebuilt[:8])}", flush=True)
    print(f"STAGE {stage} {'FAIL' if failed else 'PASS'}: {len(outcomes)} gates, "
          f"{len(failed)} failed, {time.monotonic() - checks_started:.1f}s in gates"
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


def selected_gates(stages, database, chosen, requested):
    """Resolve every requested name against actual checks in the selected stages.

    A nested gate can be run directly, without its parent's other checks.
    A named aggregate still expands to all of its checks.
    """
    if not requested:
        return {stage: expand_aggregates(stages[stage], database)
                for stage in chosen}, []
    prerequisites, _, phony, _ = database
    by_stage = {stage: [] for stage in chosen}
    reached = {stage: set(stages[stage]).union(
        *(closure(prerequisites, gate) for gate in stages[stage]))
        for stage in chosen}
    missing = []
    for target in requested:
        for stage in chosen:
            root = "test-all" if stage == "correctness" else f"test-{stage}"
            if target == root and stages[stage]:
                candidates = expand_aggregates(stages[stage], database)
            elif target in reached[stage] and is_gate(target, phony):
                candidates = expand_aggregates([target], database)
            else:
                continue
            for gate in candidates:
                if gate not in by_stage[stage]:
                    by_stage[stage].append(gate)
            break
        else:
            missing.append(target)
    return by_stage, missing


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
    plan, missing = selected_gates(stages, database, chosen, args.target)
    if missing:
        print(f"FAIL: no gate matches in the selected stages: {', '.join(missing)}")
        return 2
    started = time.monotonic()
    ran = False
    for stage in chosen:
        gates = plan[stage]
        if args.failed:
            gates = [gate for gate in gates if previous.get(gate, ("",))[0] == "fail"]
        ran |= bool(gates)
        if gates and run_stage(root, state, stage, gates, database, stats_database, jobs,
                               previous, args.dry_run):
            print(f"SUITE FAIL at the {stage} stage, {time.monotonic() - started:.0f}s; "
                  f"logs in {(state / 'logs').relative_to(root)}")
            return 1
    if not ran:
        print("No recorded failures selected; no gates ran." if args.failed
              else "No gates selected; no gates ran.")
        return 0 if args.failed else 2
    elif not args.dry_run:
        print(f"SUITE PASS: {', '.join(chosen)}, {time.monotonic() - started:.0f}s")
    return 0
