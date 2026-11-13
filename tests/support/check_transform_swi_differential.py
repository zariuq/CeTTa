#!/usr/bin/env python3
"""transform written in ordinary PeTTa, on SWI-PeTTa, against the native firing.

Usage: check_transform_swi_differential.py CETTA_BINARY --swi PETTA_ROOT
           [--programs N] [--seed S] [--report FILE]

PETTA_ROOT is a SWI-PeTTa tree with an executable run.sh.  Each program runs
four ways:

    native       CeTTa, (library lib_transform)
    specs        CeTTa, lib_transform_specs
    swi-specs    SWI-PeTTa, lib_transform_specs imported by path
    swi-library  SWI-PeTTa, lib_transform_swi imported by path

Answers and stores are compared as bags, statement by statement, on the
statements the program labels.  The fixed programs are tests/petta/transform_*
with one library import; the seeded programs come in classes:

    ground   the generator of check_transform_differential (ground stores)
    guard    a let guard over the row's bindings
    spaces   conjuncts that name a second space
    route    a function destination
    open     stored atoms that hold variables

The class `open` is reported apart: PeTTa's remove-atom removes every atom its
argument unifies with, so a take written with it is exact on ground stores
only.  Every other class must agree on all four.

SWI-PeTTa runs hyperpose branches in threads.  One more check runs eight
parallel firings there after (tf:atomic!), with each PeTTa-written library:
one token is taken once, and a counter stays one row.
"""
import argparse
import glob
import os
import random
import signal
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_transform_differential as base  # noqa: E402

ROOT = base.ROOT
LIBRARY_IMPORT = base.LIBRARY_IMPORT
WAYS = ["native", "specs", "swi-specs", "swi-library"]


def path_import(name):
    return f"!(import! &self {os.path.join(ROOT, 'lib', 'petta', name + '.metta')})"


def labels_of(source):
    """The labels of a program's statements: !(LABEL ...)."""
    found = []
    for line in source.splitlines():
        line = line.strip()
        if line.startswith("!(") and len(line) > 2 and line[2] not in "()":
            word = line[2:].split()[0].rstrip(")")
            if word not in ("import!", "bind!", "add-atom", "remove-atom") and word not in found:
                found.append(word)
    return found


def renamed(atom, names):
    """ATOM with its variables named by first appearance: an engine prints an
    unbound variable under a name of its own."""
    if isinstance(atom, tuple):
        return tuple(renamed(item, names) for item in atom)
    if atom.startswith("$"):
        return names.setdefault(atom, f"$v{len(names)}")
    return atom


def written(atom):
    if isinstance(atom, tuple):
        return "(" + " ".join(written(item) for item in atom) + ")"
    return atom


def variables_named(text):
    """One output line with variables named apart from the engine: within each
    answer of a collapse, or within the line."""
    try:
        atoms = base.atoms_of(text)
    except (IndexError, ValueError):
        return text
    if len(atoms) == 1 and isinstance(atoms[0], tuple) and len(atoms[0]) == 2 \
            and isinstance(atoms[0][1], tuple):
        label, answers = atoms[0]
        return written((label, tuple(renamed(answer, {}) for answer in answers)))
    return " ".join(written(renamed(atom, {})) for atom in atoms)


def labelled(output, labels):
    """The output lines that answer a labelled statement."""
    kept = []
    for line in output.splitlines():
        text = line.strip()
        if text.startswith("[") and text.endswith("]"):
            text = text[1:-1].strip()
        if text.startswith("(") and len(text) > 1:
            head = text[1:].split()[0].rstrip(")") if text[1:].split() else ""
            if head in labels:
                kept.append(variables_named(text))
    return "\n".join(kept)


def read_forms(source):
    """The top-level forms of SOURCE as nested lists of tokens; comments dropped.
    A leading "!" is kept as the token before its form."""
    tokens, i = [], 0
    while i < len(source):
        c = source[i]
        if c == ";":
            while i < len(source) and source[i] != "\n":
                i += 1
        elif c in "()":
            tokens.append(c)
            i += 1
        elif c.isspace():
            i += 1
        elif c == '"':
            j = i + 1
            while j < len(source) and source[j] != '"':
                j += 2 if source[j] == "\\" else 1
            tokens.append(source[i:j + 1])
            i = j + 1
        elif c == "!" and i + 1 < len(source) and source[i + 1] == "(":
            tokens.append("!")
            i += 1
        else:
            j = i
            while j < len(source) and not source[j].isspace() and source[j] not in "();":
                j += 1
            tokens.append(source[i:j])
            i = j

    def form(k):
        if tokens[k] != "(":
            return tokens[k], k + 1
        items, k = [], k + 1
        while tokens[k] != ")":
            item, k = form(k)
            items.append(item)
        return items, k + 1

    forms, k = [], 0
    while k < len(tokens):
        item, k = form(k)
        forms.append(item)
    return forms


def write_form(form):
    if isinstance(form, list):
        return "(" + " ".join(write_form(item) for item in form) + ")"
    return form


def write_forms(forms):
    lines, bang = [], False
    for form in forms:
        if form == "!":
            bang = True
            continue
        lines.append(("!" if bang else "") + write_form(form))
        bang = False
    return "\n".join(lines) + "\n"


THREE_ARGUMENTS = "(: transform (-> Atom Atom Atom %Undefined%))\n"


def one_declaration(work, name, text=None):
    """The library NAME for SWI-PeTTa, as a file in WORK.  stable-fixed c3adb86
    runs a call once for each type declaration of its head whose arity is at
    least the call's; transform has declarations of three and of four
    arguments, so a three-argument call would run twice there.  The copy keeps
    the four-argument declaration alone, under which a three-argument call
    runs once with its arguments held.  Programs stay as written, so a
    transform expression held as data is not touched."""
    path = os.path.join(work, name + "_one_declaration.metta")
    if text is None and os.path.exists(path):
        return path
    if text is None:
        with open(os.path.join(ROOT, "lib", "petta", name + ".metta")) as stream:
            text = stream.read()
    if text.count(THREE_ARGUMENTS) != 1:
        raise SystemExit(f"FAIL: {name} no longer declares transform at three arguments as expected")
    with open(path, "w") as stream:
        stream.write(text.replace(THREE_ARGUMENTS, ""))
    return path


def for_swi(source, library, work):
    """SOURCE for SWI-PeTTa: the library imported by path, the program as written."""
    return source.replace(LIBRARY_IMPORT, f"!(import! &self {one_declaration(work, library)})")


def run_cetta(binary, source, work, name):
    path = os.path.join(work, name + ".metta")
    with open(path, "w") as stream:
        stream.write(source)
    p = subprocess.run([binary, "--lang", "petta", path], cwd=ROOT, capture_output=True,
                       text=True, timeout=120,
                       env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1"))
    return p.returncode, p.stdout, p.stderr


LIVE = set()


def stop_group(pid):
    try:
        os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def stop_live_groups(number, frame):
    """The check itself is being stopped: stop what it started, then stop."""
    for pid in list(LIVE):
        stop_group(pid)
    if number == signal.SIGINT:
        signal.default_int_handler(number, frame)
    sys.exit(128 + number)


if threading.current_thread() is threading.main_thread():
    signal.signal(signal.SIGTERM, stop_live_groups)
    signal.signal(signal.SIGINT, stop_live_groups)


def run_group(command, cwd, timeout, env=None, limit=None):
    """Run COMMAND: (status, output, errors), status 124 when stopped for time.

    The command gets a process group of its own, and stopping it stops the
    group.  `sh run.sh` starts the Prolog system as a child of the shell;
    stopping the shell alone leaves that child running with no parent."""
    def prepare():
        os.setpgid(0, 0)
        if limit:
            limit()
    p = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True, preexec_fn=prepare)
    LIVE.add(p.pid)
    try:
        out, err = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        stop_group(p.pid)
        p.communicate()
        return 124, "", "timeout"
    finally:
        stop_group(p.pid)
        LIVE.discard(p.pid)
    return p.returncode, out, err


def run_swi(swi, source, work, name):
    path = os.path.join(work, name + ".metta")
    with open(path, "w") as stream:
        stream.write(source)
    return run_group(["sh", "run.sh", path, "--silent"], swi, 120)


def four_ways(binary, swi, source, work, stem):
    """The labelled answers of SOURCE on each way, and what went wrong on a way
    before its answers can be compared: a status other than 0, or a labelled
    statement with no answer line."""
    labels = labels_of(source)
    runs = [("native", lambda: run_cetta(binary, source, work, stem + "-native")),
            ("specs", lambda: run_cetta(binary, source.replace(LIBRARY_IMPORT, base.SPECS_IMPORT),
                                        work, stem + "-specs")),
            ("swi-specs", lambda: run_swi(swi, for_swi(source, "lib_transform_specs", work),
                                          work, stem + "-swi-specs")),
            ("swi-library", lambda: run_swi(swi, for_swi(source, "lib_transform_swi", work),
                                            work, stem + "-swi-library"))]
    outputs, problems, expected = {}, {}, None
    for way, run in runs:
        status, out, _ = run()
        outputs[way] = labelled(out, labels)
        answered = sorted(line[1:].split()[0].rstrip(")") for line in outputs[way].splitlines())
        if way == "native":
            expected = answered
        if status != 0:
            problems[way] = (f"status {status}", None)
        elif not answered:
            problems[way] = ("no labelled answer", None)
        elif answered != expected:
            problems[way] = ("native answers " + " ".join(expected)[:120],
                             "this way answers " + " ".join(answered)[:120])
    return outputs, problems


def differing(outputs):
    """The ways that differ from native, with the first differing statement."""
    reference = base.canonical(outputs["native"])
    found = {}
    for way in WAYS[1:]:
        other = base.canonical(outputs[way])
        if other != reference:
            at = next((i for i, (a, b) in enumerate(zip(reference, other)) if a != b),
                      min(len(reference), len(other)))
            found[way] = (reference[at] if at < len(reference) else None,
                          other[at] if at < len(other) else None)
    return found


# ---- seeded classes ---------------------------------------------------------------

def ground(rng):
    return base.program(rng)


def with_guard(rng):
    """Numbers in the store and a let guard over a row's binding."""
    lines = [base.HEADER, "!(bind! &s (new-space))\n"]
    for _ in range(rng.randint(5, 10)):
        lines.append(f"!(add-atom &s (n {rng.choice(base.CONSTANTS)} {rng.randint(0, 6)}))\n")
    for index in range(rng.randint(3, 5)):
        bound = rng.randint(1, 5)
        test = rng.choice([f"(> $v {bound})", f"(<= $v {bound})", f"(== $v {bound})"])
        access = rng.choice(["(n $k $v)", "(take (n $k $v))"])
        wrap = rng.choice(["", "", "all", "once"])
        body = f"{access} (let True {test})"
        if rng.random() < 0.4:
            body += " (let $w (+ $v 1))"
            out = "(out $k $w)"
        else:
            out = "(out $k $v)"
        src = f"({wrap} (, {body}))" if wrap else f"(, {body})"
        destination = " &s" if rng.random() < 0.3 else ""
        lines.append(f"!(l{index} (collapse (transform &s {src} {out}{destination})))\n")
        lines.append(f"!(s{index} (show &s))\n")
    return "".join(lines)


def with_spaces(rng):
    """Conjuncts that name a second space."""
    lines = [base.HEADER, "!(bind! &s (new-space))\n", "!(bind! &t (new-space))\n"]
    for space in ("&s", "&t"):
        for _ in range(rng.randint(3, 7)):
            fact, _ = base.term(rng, rng.choice(sorted(base.RELATIONS)))
            for name in base.VARIABLES:
                fact = fact.replace(name, rng.choice(base.CONSTANTS))
            lines.append(f"!(add-atom {space} {fact})\n")
    for index in range(rng.randint(3, 6)):
        conjuncts, bound = [], set()
        for _ in range(rng.randint(1, 3)):
            pattern, names = base.term(rng, rng.choice(sorted(base.RELATIONS)))
            space = rng.choice(["", "&t ", "&s "])
            kind = rng.choice(["read", "read", "take", "absent"])
            if kind == "read":
                conjuncts.append(f"(read {space}{pattern})")
                bound |= names
            elif kind == "take":
                conjuncts.append(f"(take {space}{pattern})")
                bound |= names
            else:
                conjuncts.append(f"(absent {space}{pattern})")
        out = f"(out {' '.join(sorted(bound))})" if bound else "(out)"
        destination = rng.choice(["", " &s", " &t"])
        lines.append(f"!(l{index} (collapse (transform &s (, {' '.join(conjuncts)}) "
                     f"{out}{destination})))\n")
        lines.append(f"!(s{index} (show &s))\n")
        lines.append(f"!(t{index} (show &t))\n")
    return "".join(lines)


def with_route(rng):
    """A function destination, applied to each instance after the commit."""
    lines = [base.HEADER, "(= (wrap $x) (wrapped $x))\n",
             "(= (twice $x) (superpose ((first $x) (second $x))))\n",
             "!(bind! &s (new-space))\n"]
    for _ in range(rng.randint(5, 9)):
        fact, _ = base.term(rng, rng.choice(sorted(base.RELATIONS)))
        for name in base.VARIABLES:
            fact = fact.replace(name, rng.choice(base.CONSTANTS))
        lines.append(f"!(add-atom &s {fact})\n")
    for index in range(rng.randint(3, 5)):
        pattern, names = base.term(rng, rng.choice(sorted(base.RELATIONS)))
        access = rng.choice([pattern, f"(take {pattern})", f"(all (take {pattern}))"])
        out = f"(out {' '.join(sorted(names))})" if names else "(out)"
        if rng.random() < 0.3:
            out = f"(, {out} (seen))"
        route = rng.choice(["wrap", "twice"])
        lines.append(f"!(l{index} (collapse (transform &s {access} {out} {route})))\n")
        lines.append(f"!(s{index} (show &s))\n")
    return "".join(lines)


def with_open(rng):
    """Stored atoms that hold variables, read and taken."""
    lines = [base.HEADER, "!(bind! &s (new-space))\n"]
    for _ in range(rng.randint(5, 9)):
        fact, _ = base.term(rng, rng.choice(sorted(base.RELATIONS)))
        for name in base.VARIABLES:
            if rng.random() < 0.7:
                fact = fact.replace(name, rng.choice(base.CONSTANTS))
        lines.append(f"!(add-atom &s {fact})\n")
    for index in range(rng.randint(3, 5)):
        pattern, names = base.term(rng, rng.choice(sorted(base.RELATIONS)))
        access = rng.choice([pattern, f"(take {pattern})", f"(take {pattern})",
                             f"(once (take {pattern}))"])
        out = f"(out {' '.join(sorted(names))})" if names else "(out)"
        lines.append(f"!(l{index} (collapse (transform &s {access} {out})))\n")
        lines.append(f"!(s{index} (collapse (match &s $any $any)))\n")
    return "".join(lines)


def with_values(rng):
    """A value the host can apply and that is no atom of the data model (a partial
    application), held by a caller's variable that a pattern, a stored atom or a
    template mentions."""
    lines = [base.HEADER, "!(bind! &s (new-space))\n"]
    values = ["(+ 1)", "(* 2)", "(- 10)"]
    for _ in range(rng.randint(4, 8)):
        lines.append(f"!(add-atom &s (n {rng.choice(base.CONSTANTS)} {rng.randint(0, 6)}))\n")
    for value in rng.sample(values, 2):
        lines.append(f"!(let $f {value} (add-atom &s (holds $f)))\n")
    for index in range(rng.randint(3, 5)):
        value = rng.choice(values)
        access, out = rng.choice([
            ("(n $k $v)", "(out $k $f)"),
            ("(take (n $k $v))", "(out $f $v)"),
            ("(holds $f)", "(held)"),
            ("(holds $g)", "(out $g $f)"),
            ("(all (take (n $k $v)))", "(out $k $f)"),
            ("(, (n $k $v) (holds $f))", "(out $k)"),
        ])
        destination = " &s" if rng.random() < 0.25 else ""
        lines.append(f"!(l{index} (collapse (let $f {value} (transform &s {access} {out}{destination}))))\n")
        lines.append(f"!(s{index} (show &s))\n")
    return "".join(lines)


def with_consumer(rng):
    """The caller acts on the store between the answers of one transform: it adds
    the atom whose absence the later branches require, or takes what they read.
    A branch fires when its answer is asked for, against the store as it is then."""
    lines = [base.HEADER, "!(bind! &s (new-space))\n", "!(bind! &w (new-space))\n"]
    for number in range(rng.randint(3, 6)):
        lines.append(f"!(add-atom &s (r {number}))\n")
    for index in range(rng.randint(2, 4)):
        kind = rng.choice(["stop-elsewhere", "stop-here", "take-ahead"])
        if kind == "stop-elsewhere":
            lines.append(f"!(l{index} (collapse (let $k (transform &s (, (r $x) (absent &w (stop {index}))) $x)"
                         f" (let $_ (add-atom &w (stop {index})) $k))))\n")
        elif kind == "stop-here":
            lines.append(f"!(l{index} (collapse (let $k (transform &s (, (r $x) (absent (stop {index}))) $x)"
                         f" (let $_ (add-atom &s (stop {index})) $k))))\n")
        else:
            lines.append(f"!(l{index} (collapse (let $k (transform &s (r $x) $x)"
                         f" (let $_ (remove-atom &s (r (+ $k 1))) $k))))\n")
        lines.append(f"!(s{index} (show &s))\n")
    return "".join(lines)


def with_choice(rng):
    """A kernel let with several answers: each answer is a match of the kernel, so
    each is a branch of its own, and a branch fires the answer it stands for."""
    lines = [base.HEADER, "!(bind! &s (new-space))\n"]
    for number in range(rng.randint(2, 4)):
        lines.append(f"!(add-atom &s (n {number}))\n")
    lines.append("!(add-atom &s (tok))\n")
    for index in range(rng.randint(3, 5)):
        answers = " ".join(rng.choice(base.CONSTANTS) for _ in range(rng.randint(2, 4)))
        source, out = rng.choice([
            (f"(let $x (superpose ({answers})))", "(out $x)"),
            (f"(, (n $k) (let $x (superpose ({answers}))))", "(out $k $x)"),
            (f"(, (take (n $k)) (let $x (superpose ({answers}))))", "(out $k $x)"),
            (f"(, (let $x (superpose ({answers}))) (n $k))", "(out $x $k)"),
        ])
        lines.append(f"!(l{index} (collapse (transform &s {source} {out})))\n")
        lines.append(f"!(s{index} (show &s))\n")
    return "".join(lines)


CLASSES = [("ground", ground), ("guard", with_guard), ("spaces", with_spaces),
           ("route", with_route), ("values", with_values), ("open", with_open),
           ("consumer", with_consumer), ("choice", with_choice)]
# The classes every way must agree on.  The others are reported with their
# counts: stored atoms that hold variables (plain PeTTa removes by unification),
# a caller acting between answers (the native firing runs every branch at the
# call), and a kernel let with several answers (the native firing answers the
# first of them once for each).
EXACT = ["ground", "guard", "spaces", "route", "values"]
IMPORTED_HERE = ["transform_once", "transform_once_let"]
# Fixed programs this check cannot compare, each with its reason; they are
# printed with the result, never counted as agreeing.
NOT_COMPARED = {
    "transform_extended_let": "its statements are bare lets, which carry no label to compare by, "
                              "and it calls (new-space) inside a let, which SWI-PeTTa c3adb86 refuses",
}


CONTROL = (LIBRARY_IMPORT + "\n!(bind! &s (new-space))\n!(add-atom &s (job a))\n"
           "!(l0 (collapse (transform &s (, (take (job $j)) (take (job $k))) (pair $j $k))))\n"
           "!(s0 (collapse (match &s $a $a)))\n")
VALIDATION = "(>= (tfs:stored $s $a) (+ $need (if (> $read 0) 1 0)))"


def control_detected(binary, swi, work):
    """A specification whose validation always holds must be told apart: two
    takes of the one stored job then fire, where the firing takes two distinct
    occurrences or nothing."""
    with open(os.path.join(ROOT, "lib", "petta", "lib_transform_specs.metta")) as stream:
        library = stream.read()
    if library.count(VALIDATION) != 1:
        return False
    labels = labels_of(CONTROL)
    _, native, _ = run_cetta(binary, CONTROL, work, "control-native")
    mutant = one_declaration(work, "mutant_specs", library.replace(VALIDATION, "True"))
    _, wrong, _ = run_swi(swi, CONTROL.replace(LIBRARY_IMPORT, f"!(import! &self {mutant})"),
                          work, "control-mutant")
    _, right, _ = run_swi(swi, for_swi(CONTROL, "lib_transform_specs", work), work,
                          "control-specs")
    native, wrong, right = (labelled(text, labels) for text in (native, wrong, right))
    return bool(native.strip()) and base.same(native, right) and not base.same(native, wrong)


# ---- commits beside parallel branches -----------------------------------------------
# SWI-PeTTa's hyperpose runs its branches in threads.  After (tf:atomic!) the
# commit of a firing is a critical section: of eight branches taking the one
# token exactly one fires, and a counter taken and made again by eight
# branches is never doubled or lost.
PARALLEL = (LIBRARY_IMPORT + """
!(tf:atomic!)
!(add-atom &token token)
(= (claim $i) (transform &token (take token) (won $i) &winners))
!(hyperpose ((claim 1) (claim 2) (claim 3) (claim 4) (claim 5) (claim 6) (claim 7) (claim 8)))
!(println! (winners (size-atom (collapse (match &winners (won $w) $w)))))
!(add-atom &count (count 0))
(= (bump $i) (transform &count (, (take (count $n)) (let $m (+ $n 1))) (count $m) &count))
!(hyperpose ((bump 1) (bump 2) (bump 3) (bump 4) (bump 5) (bump 6) (bump 7) (bump 8)))
!(println! (counters (size-atom (collapse (match &count (count $n) $n)))))
""")
PARALLEL_RUNS = 5


def parallel_commits(swi, work):
    """For each PeTTa-written library: how many of PARALLEL_RUNS runs kept both
    invariants."""
    kept = {}
    for library in ("lib_transform_specs", "lib_transform_swi"):
        source = for_swi(PARALLEL, library, work)
        kept[library] = 0
        for number in range(PARALLEL_RUNS):
            status, out, _ = run_swi(swi, source, work, f"parallel-{library}-{number}")
            lines = [line.strip() for line in out.splitlines()]
            if status == 0 and "(winners 1)" in lines and "(counters 1)" in lines:
                kept[library] += 1
    return kept


def fixed_programs():
    found = []
    for path in sorted(glob.glob(os.path.join(ROOT, "tests", "petta", "transform_*.metta"))):
        with open(path) as stream:
            source = stream.read()
        stem = os.path.basename(path)[:-len(".metta")]
        if stem in NOT_COMPARED:
            continue
        if source.count(LIBRARY_IMPORT) == 1 and source.count("import!") == 1:
            found.append((stem, source))
        elif "import!" not in source and stem in IMPORTED_HERE:
            # written for the extended profile, where transform needs no import
            found.append((stem, LIBRARY_IMPORT + "\n" + source))
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--swi", required=True)
    parser.add_argument("--programs", type=int, default=30)
    parser.add_argument("--seed", type=int, default=20261111)
    parser.add_argument("--report")
    args = parser.parse_args()
    binary, swi = os.path.abspath(args.binary), os.path.abspath(args.swi)
    if not os.access(os.path.join(swi, "run.sh"), os.X_OK):
        print("FAIL: --swi must name a SWI-PeTTa tree with an executable run.sh")
        return 2
    rows, failed, apart = [], False, []
    with tempfile.TemporaryDirectory(prefix="cetta-transform-swi-") as work:
        if not control_detected(binary, swi, work):
            print("FAIL: a specification without validation was not told apart")
            return 1
        for stem, source in fixed_programs():
            outputs, problems = four_ways(binary, swi, source, work, stem)
            found = dict(differing(outputs), **problems)
            rows.append(("fixed", stem, found))
            failed = failed or bool(found)
        for name, generator in CLASSES:
            rng = random.Random(f"{args.seed}:{name}")
            for number in range(args.programs):
                source = generator(rng)
                stem = f"{name}_{number}"
                outputs, problems = four_ways(binary, swi, source, work, stem)
                found = dict(differing(outputs), **problems)
                written = ["specs", "swi-specs", "swi-library"]
                if any(way in problems for way in written) or not all(
                        base.same(outputs[written[0]], outputs[way]) for way in written[1:]):
                    # in every class the PeTTa-written lanes must answer alike
                    apart.append(stem)
                    failed = True
                rows.append((name, stem, found))
                if found and name in EXACT:
                    failed = True
                if found and args.report:
                    with open(args.report + "." + stem + ".metta", "w") as stream:
                        stream.write(source)
        parallel = parallel_commits(swi, work)
    lines = []
    for library, kept in sorted(parallel.items()):
        lines.append(f"parallel commits, {library} on SWI-PeTTa: one winner and one counter "
                     f"in {kept} of {PARALLEL_RUNS} runs")
        failed = failed or kept != PARALLEL_RUNS
    for kind in ["fixed"] + [name for name, _ in CLASSES]:
        mine = [row for row in rows if row[0] == kind]
        agree = sum(1 for row in mine if not row[2])
        by_way = {way: sum(1 for row in mine if way in row[2]) for way in WAYS}
        lines.append(f"{kind:8s} programs {len(mine):3d}  all four agree {agree:3d}  "
                     + "  ".join(f"{way} differs {by_way[way]}" for way in WAYS[1:]))
        for _, stem, found in mine:
            for way, (reference, other) in found.items():
                lines.append(f"    DIFF {stem} {way}\n        native {str(reference)[:300]}\n"
                             f"        {way} {str(other)[:300]}")
    if apart:
        lines.append("the PeTTa-written lanes differ from each other: " + " ".join(apart[:12]))
    else:
        lines.append("the three PeTTa-written lanes answer alike in every class")
    for stem, reason in sorted(NOT_COMPARED.items()):
        lines.append(f"not compared: {stem}: {reason}")
    text = "\n".join(lines)
    print(text)
    if args.report:
        with open(args.report, "w") as stream:
            stream.write(text + "\n")
    if failed:
        print("FAIL: transform written in PeTTa differs from the native firing "
              "in a class where every way must agree")
        return 1
    print("PASS: transform written in PeTTa answers as the native firing on SWI-PeTTa "
          "and on CeTTa in the classes " + ", ".join(EXACT) + "; the other classes are reported above")
    return 0


if __name__ == "__main__":
    sys.exit(main())
