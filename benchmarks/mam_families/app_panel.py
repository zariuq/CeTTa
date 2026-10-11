#!/usr/bin/env python3
"""Generate application programs for native MM2, HE and PeTTa with declared observations.

    app_panel.py OUTDIR [--only FAMILY[,FAMILY...]] [--scale smoke|measure]

The families scale the programs MORK ships: ancestor, transitive closure,
counter machine, odd-even sort, grounded bulk compute and the counting step
of decision-tree learning. A problem instance has the MM2 program in MORK's
own idiom and one case per MeTTa program form:

  natural      the solution a MeTTa programmer writes for the problem
  tabled       the natural solution with the recursive relation tabled (PeTTa)
  translation  the MM2 algorithm written in MeTTa

Every expected result is computed here in Python, never by an engine. MORK's
store is a set, so an observation is a set of result atoms or one result atom.
Where MM2 and MeTTa spell the same answer differently (unary numerals against
integers, array facts against a list) the case carries one expected file per
spelling, both written from the same Python value.

The output directory has the layout of join_panel.py and path_panel.py and
runs under `run.py --paired`. The tabled cases import lib_tabling, so the
CeTTa executable must be one that finds its library directory.
"""
import argparse
import hashlib
import json
import math
import random
from pathlib import Path

TABLING = "!(import! &self (library lib_tabling))\n"
TABLING_PREAMBLE = ["true", "true"]


def peano(n):
    return "Z" if n == 0 else "(S " * n + "Z" + ")" * n


def facts(head, rows):
    return "".join(f"({head} {' '.join(str(x) for x in row)})\n" for row in rows)


def semantics(problem, observer, multiplicity, mm2_plan, mm2_control, **extra):
    return dict(problem=problem, observer=observer, order="unobserved",
                multiplicity=multiplicity, read_window="frozen input facts",
                mm2_plan=mm2_plan, mm2_control=mm2_control,
                publication="CeTTa and PeTTa result stream; native MORK final store "
                            "projected on the result heads", **extra)


# ---------------------------------------------------------------- ancestors --

def ancestor_graph(shape, width, depth):
    """Edges (parent, child) of a layered family graph and its point of interest.

    Layer 0 holds the point of interest; layer k+1 holds parents of layer k.
      chain    one parent per person
      tree     two parents per person, never shared (a pedigree)
      lattice  two parents per person, shared with the neighbour (diamonds)
    """
    def name(layer, index):
        return f"p{layer}x{index}"
    edges = []
    if shape == "chain":
        for layer in range(depth):
            edges.append((name(layer + 1, 0), name(layer, 0)))
    elif shape == "tree":
        for layer in range(depth):
            for index in range(2 ** layer):
                edges.append((name(layer + 1, 2 * index), name(layer, index)))
                edges.append((name(layer + 1, 2 * index + 1), name(layer, index)))
    elif shape == "lattice":
        for layer in range(depth):
            for index in range(width):
                edges.append((name(layer + 1, index), name(layer, index)))
                edges.append((name(layer + 1, (index + 1) % width), name(layer, index)))
    else:
        raise ValueError(shape)
    return edges, name(0, 0)


def unrelated_families(count, depth):
    """Chains of other families: facts of the same relation the query never reads."""
    return [(f"q{family}x{layer + 1}", f"q{family}x{layer}")
            for family in range(count) for layer in range(depth)]


def ancestors_oracle(edges, start):
    """Set of ancestors by graph search, and the number of upward walks."""
    parents = {}
    for parent, child in edges:
        parents.setdefault(child, []).append(parent)
    seen, frontier = set(), [start]
    while frontier:
        following = []
        for person in frontier:
            for parent in parents.get(person, ()):
                if parent not in seen:
                    seen.add(parent)
                    following.append(parent)
        frontier = following
    walks = {}
    stack = [start]
    while stack:
        person = stack[-1]
        waiting = [parent for parent in parents.get(person, ()) if parent not in walks]
        if waiting:
            stack.extend(waiting)
            continue
        walks[person] = sum(1 + walks[parent] for parent in parents.get(person, ()))
        stack.pop()
    return seen, walks[start]


def family_facts(edges, layout):
    """The parent relation as MORK's example stores it, or with the child first."""
    if layout == "parent_first":
        return facts("parent", edges)
    return facts("child", [(child, parent) for parent, child in edges])


def parent_pattern(layout):
    return "(parent $p $c)" if layout == "parent_first" else "(child $c $p)"


def ancestor_mm2(edges, start, layout):
    # MORK's ancestor.mm2: a level-indexed rule that schedules itself while a
    # level still has members. The store is a set, so shared ancestors merge.
    # A path store finds facts by their leading arguments, so the shipped
    # program first writes the relation with the child in front.
    index = "" if layout == "child_first" else """(exec (0 0) (, (parent $p $c))
            (, (child $c $p)))
"""
    rules = """(exec (0 1) (, (poi $c) (child $c $p))
            (, (generation Z $c $p)))
(exec (1 Z) (, (exec (1 $l) $ps $ts)
               (generation $l $c $p) (child $p $gp))
            (, (exec (1 (S $l)) $ps $ts)
               (generation (S $l) $c $gp)))
(exec (2 0) (, (generation $_ $p $a))
            (, (ancestor $p $a)))
"""
    return family_facts(edges, layout) + f"(poi {start})\n" + index + rules


def ancestor_relation(layout):
    pattern = parent_pattern(layout)
    return (f"(= (ancestors $c) (match &self {pattern} $p))\n"
            f"(= (ancestors $c) (match &self {pattern} (ancestors $p)))\n")


def ancestor_natural(edges, start, layout):
    # An ancestor is a parent or a parent's ancestor; each one is reported once.
    return family_facts(edges, layout) + ancestor_relation(layout) + \
        f"!(let $a (unique (ancestors {start})) (ancestor {start} $a))\n"


def ancestor_collected(dialect, edges, start, layout):
    # The same relation; the answers are collected and made unique as a list.
    if dialect == "he":
        query = (f"!(let $all (collapse (ancestors {start})) (let $set (unique-atom $all) "
                 f"(let $a (superpose $set) (ancestor {start} $a))))\n")
    else:
        query = (f"!(let $set (unique-atom (collapse (ancestors {start}))) "
                 f"(let $a (superpose $set) (ancestor {start} $a)))\n")
    return family_facts(edges, layout) + ancestor_relation(layout) + query


def ancestor_tabled(edges, start, layout):
    # The same relation, tabled: a person's ancestors are derived once.
    return TABLING + "!(tabled (ancestors $c))\n" + family_facts(edges, layout) + \
        ancestor_relation(layout) + f"!(let $a (ancestors {start}) (ancestor {start} $a))\n"


def ancestor_tabled_equations(edges, start):
    # The parent relation as a function given by equations, not as facts read
    # from the space; the recursive relation is tabled as before.
    return TABLING + "!(tabled (ancestors $c))\n" + \
        "".join(f"(= (parent-of {child}) {parent})\n" for parent, child in edges) + \
        "(= (ancestors $c) (parent-of $c))\n(= (ancestors $c) (ancestors (parent-of $c)))\n" + \
        f"!(let $a (ancestors {start}) (ancestor {start} $a))\n"


def ancestor_levelwise(dialect, edges, start, layout):
    # The MM2 algorithm: one generation at a time, each generation a set.
    pattern = parent_pattern(layout)
    if dialect == "he":
        program = f"""(= (parents-of $people)
   (let $found (collapse (let $c (superpose $people) (match &self {pattern} $p)))
        (unique-atom $found)))
(= (generations $people $seen)
   (if (== $people ())
       $seen
       (let $next (parents-of $people)
            (let $all (union-atom $seen $next)
                 (generations $next $all)))))
"""
        query = (f"!(let $all (generations ({start}) ()) (let $set (unique-atom $all) "
                 f"(let $a (superpose $set) (ancestor {start} $a))))\n")
    else:
        program = f"""(= (parents-of $people)
   (unique-atom (collapse (let $c (superpose $people) (match &self {pattern} $p)))))
(= (generations $people $seen)
   (if (== $people ())
       $seen
       (let $next (parents-of $people)
            (generations $next (union-atom $seen $next)))))
"""
        query = (f"!(let $set (unique-atom (generations ({start}) ())) "
                 f"(let $a (superpose $set) (ancestor {start} $a)))\n")
    return family_facts(edges, layout) + program + query


def ancestor_cases(scale):
    first, keyed = "parent_first", "child_first"
    plans = [("chain", 1, 16, 0, first), ("tree", 1, 4, 0, first), ("lattice", 4, 4, 0, first),
             ("chain", 1, 8, 20, first), ("lattice", 4, 4, 0, keyed)] \
        if scale == "smoke" else [
        ("chain", 1, 64, 0, first), ("chain", 1, 256, 0, first), ("chain", 1, 1024, 0, first),
        ("tree", 1, 8, 0, first), ("tree", 1, 10, 0, first), ("tree", 1, 12, 0, first),
        ("lattice", 8, 8, 0, first), ("lattice", 8, 12, 0, first), ("lattice", 8, 16, 0, first),
        ("chain", 1, 64, 1600, first), ("chain", 1, 64, 6400, first),
        ("tree", 1, 12, 0, keyed), ("lattice", 8, 16, 0, keyed), ("chain", 1, 64, 1600, keyed)]
    for shape, width, depth, others, layout in plans:
        edges, start = ancestor_graph(shape, width, depth)
        ancestors, walks = ancestors_oracle(edges, start)
        edges = edges + unrelated_families(others, depth)
        expected = sorted(f"(ancestor {start} {person})" for person in ancestors)
        label = f"{shape}_{depth}" if shape != "lattice" else f"lattice_{width}x{depth}"
        if others:
            label += f"_in_{len(edges)}"
        if layout == keyed:
            label += "_childfirst"
        base = dict(
            family="ancestor", instance=f"ancestor_{label}", expected=expected,
            mm2=ancestor_mm2(edges, start, layout),
            projection_heads=["ancestor"], mm2_steps=depth + 8,
            params=dict(shape=shape, width=width, depth=depth, facts=len(edges),
                        unrelated_facts=others * depth, layout=layout,
                        distinct_ancestors=len(ancestors), upward_walks=walks),
            oracle="graph search over the generated parent relation")
        common = semantics(
            problem="all ancestors of one person in a layered family graph",
            observer="set of ancestors", multiplicity="one result per distinct ancestor",
            mm2_plan="MORK ancestor.mm2: a level-indexed rule reschedules itself and the set "
                     "store merges shared ancestors; with parents stored first the program "
                     "begins by writing the relation with the child in front",
            mm2_control="exec agenda in location order until no exec remains",
            layout="facts (parent P C) as in MORK's example" if layout == first
                   else "facts (child C P): the person asked about is the leading argument")
        yield dict(base, case=f"ancestor_{label}_natural", role="natural",
                   he=ancestor_natural(edges, start, layout),
                   petta=ancestor_natural(edges, start, layout),
                   semantics=dict(common, comparison="idiomatic solutions to the same problem",
                                  metta_plan="recursive relation, every upward walk explored, "
                                             "answers made unique as a stream",
                                  metta_control="one query evaluated to exhaustion"))
        yield dict(base, case=f"ancestor_{label}_collected", role="natural",
                   he=ancestor_collected("he", edges, start, layout),
                   petta=ancestor_collected("petta", edges, start, layout),
                   semantics=dict(common, comparison="idiomatic solutions to the same problem",
                                  metta_plan="recursive relation, every upward walk explored, "
                                             "answers collected and made unique as a list",
                                  metta_control="one query evaluated to exhaustion"))
        tabled = dict(common, comparison="idiomatic solutions to the same problem",
                      metta_control="one query evaluated to exhaustion; tables complete "
                                    "before the answers are reported")
        yield dict(base, case=f"ancestor_{label}_tabled", role="tabled",
                   petta=ancestor_tabled(edges, start, layout), petta_preamble=TABLING_PREAMBLE,
                   semantics=dict(tabled, metta_plan="recursive relation with a table per "
                                  "person; a table holds each distinct ancestor once"))
        if not others and layout == first:
            yield dict(base, case=f"ancestor_{label}_tabled_equations", role="tabled",
                       petta=ancestor_tabled_equations(edges, start),
                       petta_preamble=TABLING_PREAMBLE,
                       semantics=dict(tabled, metta_plan="as the tabled case, with the parent "
                                      "relation given by equations instead of facts in the "
                                      "space"))
        yield dict(base, case=f"ancestor_{label}_levelwise", role="translation",
                   he=ancestor_levelwise("he", edges, start, layout),
                   petta=ancestor_levelwise("petta", edges, start, layout),
                   semantics=dict(common, comparison="direct algorithm translation",
                                  metta_plan="one generation at a time, each generation a set",
                                  metta_control="one query evaluated to exhaustion"))


# ------------------------------------------------------------------ closure --

def closure_graph(shape, nodes, seed=20261009):
    """Directed graphs with cycles.

      ring    one cycle through every node: the longest shortest paths
      random  the ring's nodes with two seeded random successors each
    """
    names = [f"n{index}" for index in range(nodes)]
    if shape == "ring":
        edges = [(names[i], names[(i + 1) % nodes]) for i in range(nodes)]
    elif shape == "random":
        rng = random.Random(seed + nodes)
        chosen = set()
        for i in range(nodes):
            for target in rng.sample(range(nodes), 2):
                chosen.add((names[i], names[target]))
        edges = sorted(chosen)
    else:
        raise ValueError(shape)
    return names, edges


def closure_oracle(names, edges):
    successors = {}
    for source, target in edges:
        successors.setdefault(source, []).append(target)
    pairs, longest = set(), 0
    for source in names:
        seen, frontier, rounds = set(), [source], 0
        while frontier:
            following = []
            for node in frontier:
                for target in successors.get(node, ()):
                    if target not in seen:
                        seen.add(target)
                        following.append(target)
            frontier = following
            rounds += bool(following)
        longest = max(longest, rounds)
        pairs.update((source, target) for target in seen)
    return pairs, longest


def closure_mm2(edges, rounds):
    # MORK's transitive.mm2 joins the relation with itself once. Repeating that
    # step doubles the covered path length, so ceil(log2(longest path)) steps
    # close the relation; the set store absorbs what is derived again.
    program = facts("edge", edges) + "(exec (0 0) (, (edge $x $y)) (, (path $x $y)))\n"
    for index in range(rounds):
        program += f"(exec (1 {index}) (, (path $x $y) (path $y $z)) (, (path $x $z)))\n"
    return program


def closure_tabled(names, edges):
    # Reachability as a recursive relation; tabling makes it terminate on cycles.
    return TABLING + "!(tabled (reach $x))\n" + facts("edge", edges) + \
        facts("node", [(name,) for name in names]) + \
        """(= (reach $x) (match &self (edge $x $y) $y))
(= (reach $x) (match &self (edge $x $y) (reach $y)))
!(match &self (node $x) (let $y (reach $x) (path $x $y)))
"""


def closure_levelwise(names, edges):
    # Without tables the program keeps the set of nodes it has seen.
    return facts("edge", edges) + facts("node", [(name,) for name in names]) + \
        """(= (successors $nodes)
   (let $found (collapse (let $x (superpose $nodes) (match &self (edge $x $y) $y)))
        (unique-atom $found)))
(= (reachable $frontier $seen)
   (if (== $frontier ())
       $seen
       (let $next (successors $frontier)
            (let $fresh (subtraction-atom $next $seen)
                 (let $all (union-atom $seen $fresh)
                      (reachable $fresh $all))))))
!(match &self (node $x) (let $set (reachable ($x) ()) (let $y (superpose $set) (path $x $y))))
"""


def closure_cases(scale):
    plans = [("ring", 6), ("random", 8)] if scale == "smoke" else [
        ("ring", 16), ("ring", 32), ("ring", 64), ("ring", 128),
        ("random", 32), ("random", 64), ("random", 128)]
    for shape, nodes in plans:
        names, edges = closure_graph(shape, nodes)
        pairs, longest = closure_oracle(names, edges)
        rounds = max(1, math.ceil(math.log2(max(longest, 2))))
        expected = sorted(f"(path {source} {target})" for source, target in pairs)
        base = dict(
            family="closure", instance=f"closure_{shape}_{nodes}", expected=expected,
            mm2=closure_mm2(edges, rounds),
            projection_heads=["path"], mm2_steps=rounds + 4,
            params=dict(shape=shape, nodes=nodes, edges=len(edges), pairs=len(pairs),
                        longest_shortest_path=longest, squaring_steps=rounds),
            oracle="breadth-first search from every node of the generated graph")
        common = semantics(
            problem="every pair of nodes joined by a directed path, in a graph with cycles",
            observer="set of pairs", multiplicity="one result per distinct pair",
            mm2_plan="MORK transitive.mm2: the path relation is joined with itself; each step "
                     "doubles the covered length and the set store absorbs repeats",
            mm2_control="one exec per squaring step, in location order")
        yield dict(base, case=f"closure_{shape}_{nodes}_tabled", role="tabled",
                   petta=closure_tabled(names, edges), petta_preamble=TABLING_PREAMBLE,
                   semantics=dict(common, comparison="idiomatic solutions to the same problem",
                                  metta_plan="recursive reachability with a table per node; "
                                             "the tables make the recursion end on cycles",
                                  metta_control="one query evaluated to exhaustion; tables "
                                                "complete before the answers are reported"))
        yield dict(base, case=f"closure_{shape}_{nodes}_levelwise", role="natural",
                   he=closure_levelwise(names, edges), petta=closure_levelwise(names, edges),
                   semantics=dict(common, comparison="idiomatic solutions to the same problem",
                                  metta_plan="search from every node, one level at a time, with "
                                             "the set of nodes already seen",
                                  metta_control="one query evaluated to exhaustion"))


# ---------------------------------------------------------- counter machine --

# MORK's counter_machine_5.mm2: copy register 2 to register 3 through register 1.
COUNTER_PROGRAM = [("JZ", 2, 5), ("DEC", 2), ("INC", 3), ("INC", 1), ("JZ", 0, 0),
                   ("JZ", 1, 9), ("DEC", 1), ("INC", 2), ("JZ", 0, 5), ("H",)]


def counter_oracle(value):
    registers, counter, ticks = [0, 0, value, 0, 0], 0, 0
    while COUNTER_PROGRAM[counter][0] != "H":
        instruction = COUNTER_PROGRAM[counter]
        if instruction[0] == "JZ":
            counter = instruction[2] if registers[instruction[1]] == 0 else counter + 1
        else:
            registers[instruction[1]] += 1 if instruction[0] == "INC" else -1
            assert registers[instruction[1]] >= 0
            counter += 1
        ticks += 1
    return registers, ticks


def counter_mm2(value):
    lines = []
    for index, instruction in enumerate(COUNTER_PROGRAM):
        if instruction[0] == "JZ":
            body = f"(JZ {instruction[1]} {peano(instruction[2])})"
        elif instruction[0] == "H":
            body = "H"
        else:
            body = f"({instruction[0]} {instruction[1]})"
        lines.append(f"(program {peano(index)} {body})")
    for register in range(5):
        lines.append(f"(state Z (REG {register} {peano(value if register == 2 else 0)}))")
    lines.append("(state Z (IC Z))")
    lines.append("(if (S $n) $x $y $x)")
    lines.append("(if Z $x $y $y)")
    lines.append(" ".join(f"({a} != {b})" for a in range(5) for b in range(5) if a != b))
    # The three step rules, the halt rule and the clock are MORK's, unchanged.
    # The last exec reads the registers of the halted state.
    return "\n".join(lines) + """
((step JZ $ts)
  (, (state $ts (IC $i)) (program $i (JZ $r $j)) (state $ts (REG $r $v)) (if $v (S $i) $j $ni) (state $ts (REG $k $kv)))
  (, (state (S $ts) (IC $ni)) (state (S $ts) (REG $k $kv))))
((step INC $ts)
  (, (state $ts (IC $i)) (program $i (INC $r)) (state $ts (REG $r $v)) ($r != $o) (state $ts (REG $o $ov)))
  (, (state (S $ts) (IC (S $i))) (state (S $ts) (REG $r (S $v))) (state (S $ts) (REG $o $ov))))
((step DEC $ts)
  (, (state $ts (IC $i)) (program $i (DEC $r)) (state $ts (REG $r (S $v))) ($r != $o) (state $ts (REG $o $ov)))
  (, (state (S $ts) (IC (S $i))) (state (S $ts) (REG $r $v)) (state (S $ts) (REG $o $ov))))
((step HALT $ts)
  (, (state $ts (IC $i)) (program $i H))
  (, (HALTED $ts)))
(exec (clocked Z)
        (, (exec (clocked $ts) $p1 $t1)
           (state $ts (IC $_))
           ((step $k $ts) $p0 $t0))
        (, (exec ($k $ts) $p0 $t0)
           (exec (clocked (S $ts)) $p1 $t1)))
(exec (readout Z)
        (, (HALTED $ts) (state $ts (REG $r $v)))
        (, (register $r $v)))
"""


def counter_program_facts():
    lines = []
    for index, instruction in enumerate(COUNTER_PROGRAM):
        body = "H" if instruction[0] == "H" else "(" + " ".join(str(x) for x in instruction) + ")"
        lines.append(f"(program {index} {body})")
    return "\n".join(lines) + "\n"


def counter_natural(value):
    # An interpreter: the machine state is the argument of a recursive function.
    selectors = ""
    for register in range(5):
        names = ["$a", "$b", "$c", "$d", "$e"]
        selectors += f"(= (reg {register} ($a $b $c $d $e)) {names[register]})\n"
        names[register] = "$v"
        selectors += f"(= (put {register} $v ($a $b $c $d $e)) ({' '.join(names)}))\n"
    return counter_program_facts() + selectors + f"""(= (run $ic $regs) (match &self (program $ic $ins) (perform $ins $ic $regs)))
(= (perform H $ic $regs) $regs)
(= (perform (INC $r) $ic $regs) (run (+ $ic 1) (put $r (+ (reg $r $regs) 1) $regs)))
(= (perform (DEC $r) $ic $regs) (run (+ $ic 1) (put $r (- (reg $r $regs) 1) $regs)))
(= (perform (JZ $r $j) $ic $regs)
   (if (== (reg $r $regs) 0) (run $j $regs) (run (+ $ic 1) $regs)))
!(let ($a $b $c $d $e) (run 0 (0 0 {value} 0 0))
      (superpose ((register 0 $a) (register 1 $b) (register 2 $c) (register 3 $d) (register 4 $e))))
"""


def counter_history(value):
    # The MM2 algorithm: every tick adds the next state to the space and all
    # earlier states stay, so the space grows by six atoms per tick.
    state = "".join(f"(state 0 (REG {register} {value if register == 2 else 0}))\n"
                    for register in range(5)) + "(state 0 (IC 0))\n"
    return counter_program_facts() + state + """(= (run $ts)
   (match &self (, (state $ts (IC $i)) (program $i $ins)) (perform $ins $ts $i)))
(= (perform H $ts $i) $ts)
(= (perform (INC $r) $ts $i) (advance $ts (+ $i 1) $r 1))
(= (perform (DEC $r) $ts $i) (advance $ts (+ $i 1) $r -1))
(= (perform (JZ $r $j) $ts $i)
   (match &self (state $ts (REG $r $v))
      (if (== $v 0) (advance $ts $j $r 0) (advance $ts (+ $i 1) $r 0))))
(= (advance $ts $ni $r $delta)
   (let $n (+ $ts 1)
        (let $copied (collapse (copy-register $ts $n $r $delta))
             (let $set (add-atom &self (state $n (IC $ni)))
                  (run $n)))))
(= (copy-register $ts $n $r $delta)
   (match &self (state $ts (REG $k $v))
      (let $nv (if (== $k $r) (+ $v $delta) $v)
           (add-atom &self (state $n (REG $k $nv))))))
!(let $halt (run 0) (match &self (state $halt (REG $r $v)) (register $r $v)))
"""


def counter_cases(scale):
    for value in ((2, 4) if scale == "smoke" else (4, 16, 64, 256)):
        registers, ticks = counter_oracle(value)
        integers = [f"(register {index} {content})" for index, content in enumerate(registers)]
        unary = [f"(register {index} {peano(content)})" for index, content in enumerate(registers)]
        base = dict(
            family="counter_machine", instance=f"counter_{value}",
            expected=sorted(integers), mork_expected=sorted(unary),
            mm2=counter_mm2(value), projection_heads=["register"], mm2_steps=6 * ticks + 16,
            params=dict(register_value=value, ticks=ticks, instructions=len(COUNTER_PROGRAM)),
            oracle="direct simulation of the ten-instruction program")
        common = semantics(
            problem="run a counter machine that copies one register to another until it halts",
            observer="contents of the five registers at halt",
            multiplicity="one result per register",
            mm2_plan="MORK counter_machine_5.mm2: a clock rule schedules the step rules of the "
                     "next tick; every state is kept; numbers are unary numerals",
            mm2_control="reflective exec agenda, five execs per tick, until the machine halts",
            spelling="MM2 reports unary numerals, MeTTa integers; one simulation writes both")
        yield dict(base, case=f"counter_{value}_natural", role="natural",
                   he=counter_natural(value), petta=counter_natural(value),
                   semantics=dict(common, comparison="idiomatic solutions to the same problem",
                                  metta_plan="interpreter as a recursive function over the "
                                             "state; integer registers; nothing is stored",
                                  metta_control="one call, a tail call per tick"))
        yield dict(base, case=f"counter_{value}_history", role="translation",
                   he=counter_history(value), petta=counter_history(value),
                   semantics=dict(common, comparison="direct algorithm translation",
                                  read_window="the space grows by six atoms per tick; a tick "
                                              "reads the atoms of the tick before it",
                                  metta_plan="each tick matches the current state in the space "
                                             "and adds the next one; every state is kept",
                                  metta_control="one call, a tail call per tick"))


# --------------------------------------------------------------------- sort --

def sort_input(size, seed=20261009):
    values = list(range(size))
    random.Random(seed + size).shuffle(values)
    return values


def sort_mm2(values):
    # MORK's odd_even_sort.mm2 with a larger array. MM2 has no comparison in a
    # pattern, so the order is a table, as in the shipped program.
    size = len(values)
    lines = [" ".join(f"(lt {a} {b})" for b in range(a + 1, size)) for a in range(size - 1)]
    lines.append(" ".join(f"(succ {i} {i + 1})" for i in range(size - 1)))
    lines.append(" ".join(f"(parity {i} {'even' if i % 2 == 0 else 'odd'})" for i in range(size)))
    lines.extend(f"(A {index} {value})" for index, value in enumerate(values))
    return "\n".join(lines) + """
((phase $p)  (, (parity $i $p) (succ $i $si) (A $i $e) (A $si $se) (lt $se $e))
             (O (- (A $i $e)) (- (A $si $se)) (+ (A $i $se)) (+ (A $si $e))))
(phase 0 odd) (phase 1 even)
(exec repeat (, (A $k $_) (phase $kp $phase) ((phase $phase) $p0 $t0))
             (, (exec ($k $kp) $p0 $t0)))
"""


def cons_list(values):
    text = "Nil"
    for value in reversed(values):
        text = f"(Cons {value} {text})"
    return text


def sort_natural(values):
    # Merge sort over a list.
    return f"""(= (halves Nil) (pair Nil Nil))
(= (halves (Cons $a Nil)) (pair (Cons $a Nil) Nil))
(= (halves (Cons $a (Cons $b $t)))
   (let (pair $l $r) (halves $t) (pair (Cons $a $l) (Cons $b $r))))
(= (merged Nil $ys) $ys)
(= (merged (Cons $x $xs) Nil) (Cons $x $xs))
(= (merged (Cons $x $xs) (Cons $y $ys))
   (if (< $x $y)
       (Cons $x (merged $xs (Cons $y $ys)))
       (Cons $y (merged (Cons $x $xs) $ys))))
(= (merge-sort Nil) Nil)
(= (merge-sort (Cons $a Nil)) (Cons $a Nil))
(= (merge-sort (Cons $a (Cons $b $t)))
   (let (pair $l $r) (halves (Cons $a (Cons $b $t)))
        (merged (merge-sort $l) (merge-sort $r))))
!(sorted (merge-sort {cons_list(values)}))
"""


def sort_in_space(values):
    # The MM2 algorithm: the array lives in the space and a phase swaps every
    # adjacent pair of its parity that is out of order.
    size = len(values)
    return facts("succ", [(i, i + 1) for i in range(size - 1)]) + \
        facts("parity", [(i, "even" if i % 2 == 0 else "odd") for i in range(size)]) + \
        facts("A", list(enumerate(values))) + f"""(= (swap $i $x $j $y)
   (let* (($a (remove-atom &self (A $i $x)))
          ($b (remove-atom &self (A $j $y)))
          ($c (add-atom &self (A $i $y)))
          ($d (add-atom &self (A $j $x))))
         swapped))
(= (phase $p)
   (collapse (match &self (, (parity $i $p) (succ $i $j) (A $i $x) (A $j $y))
                (if (< $y $x) (swap $i $x $j $y) (empty)))))
(= (rounds $k)
   (if (== $k 0)
       done
       (let $odd (phase odd) (let $even (phase even) (rounds (- $k 1))))))
!(let $done (rounds {size}) (match &self (A $i $x) (A $i $x)))
"""


def sort_cases(scale):
    for size in ((6, 9) if scale == "smoke" else (16, 64, 256)):
        values = sort_input(size)
        ordered = sorted(values)
        array = sorted(f"(A {index} {value})" for index, value in enumerate(ordered))
        base = dict(
            family="sort", instance=f"sort_{size}", mm2=sort_mm2(values),
            projection_heads=["A"], mm2_steps=2 * size + 4,
            params=dict(size=size, order_table_rows=size * (size - 1) // 2, phases=2 * size),
            oracle="Python sort of the generated permutation")
        common = semantics(
            problem="sort a permutation of distinct numbers",
            observer="the sorted array", multiplicity="one result per position, or one list",
            mm2_plan="MORK odd_even_sort.mm2: a phase is one join over the pairs of its parity; "
                     "each match removes two array facts and adds two; the order is a table",
            mm2_control="two execs per position, in location order",
            spelling="array programs report one fact per position, the list program one list")
        yield dict(base, case=f"sort_{size}_natural", role="natural",
                   expected=[f"(sorted {cons_list(ordered)})"], mork_expected=array,
                   he=sort_natural(values), petta=sort_natural(values),
                   semantics=dict(common, comparison="idiomatic solutions to the same problem",
                                  metta_plan="merge sort over a list; nothing is stored",
                                  metta_control="one call"))
        yield dict(base, case=f"sort_{size}_in_space", role="translation", expected=array,
                   he=sort_in_space(values), petta=sort_in_space(values),
                   semantics=dict(common, comparison="direct algorithm translation",
                                  read_window="a phase reads the array and then replaces the "
                                              "facts of the pairs it swaps; pairs are disjoint",
                                  metta_plan="the array is facts in the space; a phase matches "
                                             "the pairs of its parity and swaps by remove and add",
                                  metta_control="one call, two phases per round"))


# --------------------------------------------------------------- arithmetic --

def arithmetic_records(count, seed=20261009):
    rng = random.Random(seed + count)
    return [(index, rng.randrange(100000)) for index in range(count)]


def arithmetic_mm2(records):
    # MORK's grounding.mm2: numbers are parsed, computed and written by one sink.
    return facts("record", records) + """(exec 0 (, (record $i $x))
        (O (pure (result $i $r) $r
             (i64_to_string (mod_i64 (sum_i64 (product_i64 (i64_from_string $x) (i64_from_string $x))
                                              (i64_from_string 7))
                                     (i64_from_string 1000))))))
"""


def arithmetic_metta(records):
    return facts("record", records) + \
        "!(match &self (record $i $x) (result $i (% (+ (* $x $x) 7) 1000)))\n"


def arithmetic_cases(scale):
    for count in ((8, 40) if scale == "smoke" else (1000, 10000, 100000)):
        records = arithmetic_records(count)
        expected = sorted(f"(result {index} {(value * value + 7) % 1000})"
                          for index, value in records)
        yield dict(
            family="arithmetic", instance=f"arithmetic_{count}", case=f"arithmetic_{count}",
            role="natural",
            expected=expected, mm2=arithmetic_mm2(records), projection_heads=["result"],
            mm2_steps=3, he=arithmetic_metta(records), petta=arithmetic_metta(records),
            params=dict(records=count),
            oracle="Python integer arithmetic on the generated records",
            semantics=dict(semantics(
                problem="compute (x*x + 7) mod 1000 for every record of a relation",
                observer="one result per record", multiplicity="one result per record",
                mm2_plan="MORK grounding.mm2: one exec; a pure sink parses the number, "
                         "computes and writes the result",
                mm2_control="one exec"),
                comparison="idiomatic solutions to the same problem",
                metta_plan="one match over the relation with grounded arithmetic in the template",
                metta_control="one query evaluated to exhaustion"))


# ------------------------------------------------------------ grouped count --

def tally_examples(examples, columns, seed=20261009):
    """Yes/no observations of every example in every column, and a yes/no label."""
    rng = random.Random(seed + examples * 31 + columns)
    observed = [(f"c{column}", f"e{example}", rng.choice(("YES", "NO")))
                for example in range(examples) for column in range(columns)]
    labelled = [(f"e{example}", rng.choice(("YES", "NO"))) for example in range(examples)]
    return observed, labelled


def tally_oracle(observed, labelled):
    label = dict(labelled)
    counts = {}
    for column, example, value in observed:
        key = (column, value, label[example])
        counts[key] = counts.get(key, 0) + 1
    return counts


def tally_mm2(observed, labelled):
    # The counting step of MORK's decision_tree_learning.mm2: one join and a
    # count sink whose template names the group.
    return facts("observed", observed) + facts("labelled", labelled) + \
        """(exec 0 (, (observed $col $id $v) (labelled $id $l))
        (O (count (tally $col $v $l $k) $k $id)))
"""


def tally_metta(dialect, observed, labelled, columns):
    # A count per group: the joined examples of a group are collected and
    # their number is taken. HE's size-atom takes its argument unevaluated,
    # so the HE program names the collection first.
    collection = "(collapse (match &self (, (observed $col $id $v) (labelled $id $l)) $id))"
    count = f"(let $ids {collection} (size-atom $ids))" if dialect == "he" \
        else f"(size-atom {collection})"
    return facts("observed", observed) + facts("labelled", labelled) + \
        facts("column", [(f"c{column}",) for column in range(columns)]) + \
        f"(= (count-labels $col $v $l)\n   {count})\n" + \
        """!(match &self (column $col)
    (let $v (superpose (YES NO))
         (let $l (superpose (YES NO))
              (let $k (count-labels $col $v $l)
                   (if (> $k 0) (tally $col $v $l $k) (empty))))))
"""


def tally_cases(scale):
    for examples, columns in (((12, 2), (40, 3)) if scale == "smoke"
                              else ((1000, 8), (10000, 8), (50000, 8))):
        observed, labelled = tally_examples(examples, columns)
        counts = tally_oracle(observed, labelled)
        expected = sorted(f"(tally {column} {value} {label} {count})"
                          for (column, value, label), count in counts.items())
        yield dict(
            family="grouped_count", instance=f"tally_{examples}x{columns}",
            case=f"tally_{examples}x{columns}", role="natural", expected=expected,
            mm2=tally_mm2(observed, labelled), projection_heads=["tally"], mm2_steps=3,
            he=tally_metta("he", observed, labelled, columns),
            petta=tally_metta("petta", observed, labelled, columns),
            params=dict(examples=examples, columns=columns, facts=len(observed) + len(labelled),
                        groups=len(counts)),
            oracle="Python tally of the generated observations",
            semantics=dict(semantics(
                problem="for every column, value and label, the number of examples that have "
                        "the value in the column and carry the label",
                observer="one count per non-empty group", multiplicity="one result per group",
                mm2_plan="the counting step of MORK decision_tree_learning.mm2: one join of "
                         "observations with labels and a count sink grouped by its template",
                mm2_control="one exec"),
                comparison="idiomatic solutions to the same problem",
                metta_plan="one count query per group: the join restricted to the group is "
                           "collected and its size taken",
                metta_control="one query evaluated to exhaustion"))


FAMILIES = {"ancestor": ancestor_cases, "closure": closure_cases, "counter_machine": counter_cases,
            "sort": sort_cases, "arithmetic": arithmetic_cases, "grouped_count": tally_cases}


def write_corpus(out, only, scale):
    out.mkdir(parents=True, exist_ok=True)
    for sub in ("he", "petta", "mork"):
        (out / sub).mkdir(exist_ok=True)

    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    manifest = []
    for family, cases in FAMILIES.items():
        if only and family not in only:
            continue
        for case in cases(scale):
            name = case["case"]
            expected = out / f"{name}.expected"
            expected.write_text("".join(line + "\n" for line in case["expected"]))
            dialects = {}
            for dialect, key, suffix in (("he", "he", "metta"), ("petta", "petta", "metta"),
                                         ("mork", "mm2", "mm2")):
                if case.get(key) is None:
                    continue
                # The cases of one problem instance share its MM2 program.
                stem = case["instance"] if dialect == "mork" else name
                source = out / dialect / f"{stem}.{suffix}"
                source.write_text(case[key])
                entry = dict(source=f"{dialect}/{stem}.{suffix}", sha256=sha(source))
                if dialect == "mork":
                    entry["steps"] = case["mm2_steps"]
                    if case.get("mork_expected") is not None:
                        other = out / f"{stem}.mork.expected"
                        other.write_text("".join(line + "\n" for line in case["mork_expected"]))
                        entry["expected"] = other.name
                        entry["expected_sha256"] = sha(other)
                else:
                    entry["profile"] = "extended"
                    if case.get(f"{dialect}_preamble"):
                        entry["preamble"] = case[f"{dialect}_preamble"]
                dialects[dialect] = entry
            manifest.append(dict(
                case=name, family=case["family"], instance=case["instance"], role=case["role"],
                params=dict(case["params"], projection_heads=case["projection_heads"]),
                answers=len(case["expected"]), expected=expected.name,
                expected_sha256=sha(expected), contract="occurrence-bag",
                dialects=dialects, oracle=case["oracle"], semantics=case["semantics"]))
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", type=Path)
    parser.add_argument("--only")
    parser.add_argument("--scale", choices=("smoke", "measure"), default="measure")
    args = parser.parse_args()
    manifest = write_corpus(args.out, set(args.only.split(",")) if args.only else None, args.scale)
    print(f"{len(manifest)} cases in {args.out}")


if __name__ == "__main__":
    main()
