#!/usr/bin/env python3
"""The step written in PeTTa (lib/petta/lib_step_rules.metta) against the reference
implementation of the calculus's step (tcalc_ref), on random firings.

Usage: check_step_rules_reference.py CETTA_BINARY DRIVER [--swi PETTA_ROOT] [--scripts N] [--seed S]

DRIVER is tests/support/step_reference_driver.c built against the reference
implementation (tcalc_ref.c, tcalc_ref.h).

A script is a sequence of firings over one state: takes and reads of earlier
makes, makes (with unknowns, minted names and stored schemas), told equations
and disequations, and guards (equal, absent, count).  Both sides say for each
firing whether it is admitted, and give the store left at the end; stores are
compared up to the names of unknowns.  Admission is a conjunction; which
failed conjunct a refusal names first is not compared, only counted.

With --swi the scripts run on SWI-PeTTa too and its agreement is reported; it
is not required, because SWI-PeTTa's unifier has no occurs check, so an equation
such as X = p(X) has a solution there and none in the calculus's finite terms.
"""
import argparse, os, random, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_transform_swi_differential as swi_lane  # noqa: E402

R = swi_lane.ROOT

SYMBOLS = {"p": 1, "q": 2, "r": 2, "c": 1}
CONSTANTS = ["a", "b"]


def term(rng, variables, depth=0, fresh=0):
    """A term as a tree: ("app", name, args), ("code", n), ("var", name), ("fresh", j)."""
    roll = rng.random()
    if depth >= 2 or roll < 0.35:
        kind = rng.choice(["var", "var", "const", "code"] + (["fresh"] if fresh else []))
        if kind == "var" and variables:
            return ("var", rng.choice(variables))
        if kind == "fresh":
            return ("fresh", rng.randrange(fresh))
        if kind == "code":
            return ("code", rng.randint(0, 3))
        return ("app", rng.choice(CONSTANTS), [])
    name = rng.choice(sorted(SYMBOLS))
    return ("app", name, [term(rng, variables, depth + 1, fresh) for _ in range(SYMBOLS[name])])


def fact(rng, variables, fresh=0):
    name = rng.choice(sorted(SYMBOLS))
    return ("app", name, [term(rng, variables, 1, fresh) for _ in range(SYMBOLS[name])])


def c_text(t):
    if t[0] == "app":
        return t[1] + ("(" + ",".join(c_text(a) for a in t[2]) + ")" if t[2] else "")
    if t[0] == "code":
        return str(t[1])
    if t[0] == "var":
        return t[1]
    return f"#{t[1]}"


def m_text(t, tag, schema=False):
    if t[0] == "app":
        return "(" + " ".join([t[1]] + [m_text(a, tag, schema) for a in t[2]]) + ")" if t[2] else t[1]
    if t[0] == "code":
        return str(t[1])
    if t[0] == "var":
        return f"(step:local {'ABC'.index(t[1])})" if schema else f"${tag}{t[1].lower()}"
    return f"${tag}n{t[1]}"


def script(rng):
    firings, makes_so_far = [], []
    for index in range(rng.randint(3, 8)):
        variables = ["X", "Y", "Z"]
        fresh = rng.choice([0, 0, 0, 1, 2])
        f = {"takes": [], "reads": [], "makes": [], "tells": [], "apart": [], "guards": [], "fresh": fresh}
        if index and makes_so_far:
            for _ in range(rng.choice([0, 1, 1, 2])):
                f["takes"].append((rng.choice(makes_so_far), fact(rng, variables)))
            for _ in range(rng.choice([0, 0, 1, 2])):
                f["reads"].append((rng.choice(makes_so_far), fact(rng, variables)))
        for j in range(rng.randint(1 if not index else 0, 3)):
            if rng.random() < 0.15:
                f["makes"].append(("schema", fact(rng, ["A", "B"])))
            else:
                f["makes"].append(("plain", fact(rng, variables, fresh)))
            makes_so_far.append((index, j))
        if index:
            for _ in range(rng.choice([0, 0, 1, 2])):
                f["tells"].append((("var", rng.choice(variables)) if rng.random() < 0.6 else term(rng, variables),
                                   term(rng, variables, 1)))
            if rng.random() < 0.25:
                f["apart"].append((("var", rng.choice(variables)), term(rng, variables, 1)))
            for _ in range(rng.choice([0, 0, 1, 1, 2])):
                kind = rng.choice(["equal", "absent", "count"])
                if kind == "equal":
                    f["guards"].append(("equal", term(rng, variables, 1), term(rng, variables, 1)))
                elif kind == "absent":
                    f["guards"].append(("absent", fact(rng, variables)))
                else:
                    f["guards"].append(("count", rng.randint(0, 2), fact(rng, variables)))
        firings.append(f)
    return firings


def c_input(firings):
    lines = []
    for f in firings:
        lines.append("firing")
        for (i, j), t in f["takes"]:
            lines.append(f"take {i}.{j} {c_text(t)}")
        for (i, j), t in f["reads"]:
            lines.append(f"read {i}.{j} {c_text(t)}")
        for kind, t in f["makes"]:
            lines.append(("schema " if kind == "schema" else "make ") + c_text(t))
        for a, b in f["tells"]:
            lines.append(f"tell {c_text(a)} = {c_text(b)}")
        for a, b in f["apart"]:
            lines.append(f"apart {c_text(a)} # {c_text(b)}")
        for g in f["guards"]:
            if g[0] == "equal":
                lines.append(f"equal {c_text(g[1])} = {c_text(g[2])}")
            elif g[0] == "absent":
                lines.append(f"absent {c_text(g[1])}")
            else:
                lines.append(f"count {g[1]} {c_text(g[2])}")
        if f["fresh"]:
            lines.append(f"fresh {f['fresh']}")
        lines.append("end")
    lines.append("dump")
    return "\n".join(lines) + "\n"


def m_program(firings, verdicts, library_import):
    """The script as one PeTTa query; identities follow the reference's verdicts."""
    ids, next_id = {}, 1
    for index, f in enumerate(firings):
        if verdicts[index] == "admitted":
            for j in range(len(f["makes"])):
                ids[(index, j)] = next_id
                next_id += 1
    texts = []
    for index, f in enumerate(firings):
        tag = f"f{index}"
        use = lambda ref, t: f"(step:use {m_text(t, tag)} {ids.get(ref, 0)})"
        takes = " ".join(use(ref, t) for ref, t in f["takes"])
        reads = " ".join(use(ref, t) for ref, t in f["reads"])
        makes = " ".join(f"(step:schema {m_text(t, tag, True)})" if kind == "schema" else m_text(t, tag)
                         for kind, t in f["makes"])
        tells = " ".join(f"(step:equal {m_text(a, tag)} {m_text(b, tag)})" for a, b in f["tells"])
        apart = " ".join(f"(step:apart {m_text(a, tag)} {m_text(b, tag)})" for a, b in f["apart"])
        guards = " ".join(
            f"(step:equal {m_text(g[1], tag)} {m_text(g[2], tag)})" if g[0] == "equal" else
            f"(step:absent {m_text(g[1], tag)})" if g[0] == "absent" else
            f"(step:count {m_text(g[2], tag)} {g[1]})" for g in f["guards"])
        names = " ".join(f"${tag}n{j}" for j in range(f["fresh"]))
        texts.append(f"(step:firing ({takes}) ({reads}) ({makes}) ({tells}) ({apart}) ({guards}) ({names}))")
    return (library_import + "\n"
            "(= (verdict-of (step:refused shapes)) shapes)\n(= (verdict-of (step:refused fits)) fits)\n"
            "(= (verdict-of (step:refused unsolvable)) unsolvable)\n"
            "(= (verdict-of (step:refused (step:guard $g))) guard)\n"
            "(= (trace $state $firings)\n"
            "   (if (== $firings ()) (cons-atom (final (labels-of $state)) ())\n"
            "     (let ($f $more) (decons-atom $firings)\n"
            "       (case (step:fire $state $f)\n"
            "         (((step:admitted $next $solved) (let $tail (trace $next $more) (cons-atom admitted $tail)))\n"
            "          ($refused (let $tail (trace $state $more) (cons-atom (verdict-of $refused) $tail))))))))\n"
            "(= (labels-of (step:state $next $names $apart $present)) (step:labels $present))\n"
            "!(result (trace (step:empty) (quote (" + " ".join(texts) + "))))\n")


def parse_c(text):
    """A reference label as a tree."""
    pos = 0

    def read():
        nonlocal pos
        start = pos
        while pos < len(text) and text[pos] not in "(),":
            pos += 1
        name = text[start:pos]
        args = []
        if pos < len(text) and text[pos] == "(":
            pos += 1
            while True:
                args.append(read())
                if text[pos] == ",":
                    pos += 1
                    continue
                pos += 1
                break
        if name[0] == "$":
            return ("var", name)
        if name[0] == "@":
            return ("name", int(name[1:]))
        if name[0] == "L" and name[1:].isdigit():
            return ("local", int(name[1:]))
        if name.lstrip("-").isdigit():
            return ("code", int(name))
        return ("app", name, args)
    return read()


def parse_m(atom):
    if isinstance(atom, tuple):
        if len(atom) == 2 and atom[0] == "step:name":
            return ("name", int(atom[1]))
        if len(atom) == 2 and atom[0] == "step:local":
            return ("local", int(atom[1]))
        if len(atom) == 2 and atom[0] == "step:schema":
            return parse_m(atom[1])
        return ("app", atom[0], [parse_m(a) for a in atom[1:]])
    if atom.startswith("$"):
        return ("var", atom)
    if atom.lstrip("-").isdigit():
        return ("code", int(atom))
    return ("app", atom, [])


def canonical(labels):
    """Unknowns named by first appearance in the store; a schema's private
    variables by first appearance in its label."""
    names = {}

    def walk(t, private):
        if t[0] == "var":
            return ("var", names.setdefault(t[1], len(names)))
        if t[0] == "local":
            return ("local", private.setdefault(t[1], len(private)))
        if t[0] == "app":
            return ("app", t[1], tuple(walk(a, private) for a in t[2]))
        return t
    return [walk(t, {}) for t in labels]


def run_metta(binary, swi, program, work, name):
    if swi:
        _, out, err = swi_lane.run_swi(swi, program, work, name)
    else:
        _, out, err = swi_lane.run_cetta(binary, program, work, name)
    line = next((l.strip() for l in out.splitlines() if l.strip().startswith("(result ")), None)
    if line is None:
        return None, (err.strip().splitlines() or ["no result"])[-1][:200]
    atoms = swi_lane.base.atoms_of(line)[0][1]
    verdicts = [a for a in atoms[:-1]]
    final = [parse_m(a) for a in atoms[-1][1]] if len(atoms[-1]) > 1 and isinstance(atoms[-1][1], tuple) else []
    return (verdicts, final), None


def lane(binary, driver, swi, scripts, seed, work):
    rng = random.Random(seed)
    if swi:
        library = (f"!(import! &self {swi_lane.one_declaration(work, 'lib_transform_specs')})\n"
                   f"!(import! &self {R}/lib/petta/lib_step_rules.metta)")
    else:
        library = "!(import! &self (library lib_transform))\n!(import! &self (library lib_step_rules))"
    agree = differ = named = firings_total = admitted_total = 0
    shown = []
    for number in range(scripts):
        firings = script(rng)
        p = subprocess.run([driver], input=c_input(firings), capture_output=True, text=True)
        lines = p.stdout.splitlines()
        c_verdicts = [l.replace("refused ", "") for l in lines[:len(firings)]]
        c_final = [parse_c(l[4:]) for l in lines[len(firings):] if l.startswith("occ ")]
        got, error = run_metta(binary, swi, m_program(firings, c_verdicts, library), work, f"s{number}")
        firings_total += len(firings)
        admitted_total += c_verdicts.count("admitted")
        admitted = lambda verdicts: [v == "admitted" for v in verdicts]
        if got is not None and admitted(got[0]) == admitted(c_verdicts) \
                and canonical(got[1]) == canonical(c_final):
            agree += 1
            named += list(got[0]) != c_verdicts
        else:
            differ += 1
            if len(shown) < 3:
                shown.append(f"    script {number}: reference {c_verdicts}\n"
                             f"               petta     {list(got[0]) if got else error}")
    return agree, differ, named, firings_total, admitted_total, shown


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("driver")
    parser.add_argument("--swi")
    parser.add_argument("--scripts", type=int, default=300)
    parser.add_argument("--seed", type=int, default=20261111)
    args = parser.parse_args()
    binary, driver = os.path.abspath(args.binary), os.path.abspath(args.driver)
    with tempfile.TemporaryDirectory(prefix="cetta-step-reference-") as work:
        agree, differ, named, firings, admitted, shown = lane(
            binary, driver, None, args.scripts, args.seed, work)
        if differ:
            print(f"FAIL: the step written in PeTTa and the reference differ on {differ} of "
                  f"{args.scripts} scripts")
            print("\n".join(shown))
            return 1
        print(f"PASS: the step written in PeTTa agrees with the reference implementation on "
              f"{agree} scripts ({firings} firings, {admitted} admitted; {named} scripts name "
              f"another failed conjunct first)")
        if args.swi:
            agree, differ, named, firings, admitted, shown = lane(
                binary, driver, os.path.abspath(args.swi), max(args.scripts // 3, 1),
                args.seed, work)
            print(f"  on SWI-PeTTa: {agree} of {agree + differ} scripts agree "
                  f"(its unifier has no occurs check)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
