#!/usr/bin/env python3
"""A match collected whole answers the same bag on the native query path as
on the ordinary one, and leaves the same spaces behind.

Usage: check_native_query.py CETTA_BINARY [--programs N] [--seed S] [--stats]

Each generated program holds a few small relations, with duplicate rows,
loops and symbols, and collects matches over them: conjunctions of one to
four patterns sharing variables, repeating a variable, fixing a constant;
in some programs binary relations without duplicate rows, which the native
path joins by rows of bits, and conjunctions over them alone; in some, rows
holding constructor patterns, which meet ground cells one way;
templates that are data, nested data, a constant or a variable, or that add
data into, or remove it from, a set space, a bag space or the matched space
itself, alone or in let and let* sequences, some of which remove what others
add, whose bodies are data or are themselves such effects, with the match's
variables or ground; at top level, through a function whose one equation is
the match, or inside a function's body.  Rows whose first column is a pair,
or a pair of pairs, meet patterns holding variables inside such terms,
repeated within a term and across patterns, and fixing parts of them, which
the native path matches part by part.  Rows holding variables or floats, at
top level or inside a pair, are among them: the native path declines those,
and they must answer the same.  Matches inside matches, over the matched
space, a bag or a set and sharing variables, are joined natively as one
conjunction; where an effect may change a row a later one of them reads,
which it reads afresh, the run makes its effects only if none met such a
read, a removal included that takes a row holding a variable which a
pattern reads, though the two fix different atoms.  Variables are read outside
the construct that binds them: a let's or a case clause's variable that
the pattern binds first or an if branch reads, a let's variable read after
the let or passed in by a caller, a collected match's variables read by
what spreads the list, an equation's parameter, ground or not, in the
collected match, one caller variable for two parameters, one the match
reads and one the spread reads; and typed functions are called, their type holding,
checking or passing their answers and arguments, where nothing reads the
collection and where something does.  Functions of several equations
whose heads hold atoms are called with one of those atoms, with none, with
a variable and with a float beside an integer, some beside a head holding a
variable, which meets every call.  Whether a match has a row is asked
in every spelling, before adding the row, inside an equation and at top
level, as a test and as a case on once with an Empty arm, with controls that compare a count with 1 or read a template
holding a variable.  Rows holding variables are added into a set and a bag
from an equation and at top level: the template's own variables, one
repeated, a caller's shared by two rows, one bound after the addition,
payloads holding a call or a variable head, a list with an open tail, a
row of 65 variables, and a value a row holding variables brought in, whose
variables a later pattern binds; then read where those variables stand.
A let destructures a matched variable inside an equation over rows holding
variables, the variable read again in the body, passed in by the caller,
or read nowhere else.  Templates are headed by a matched variable whose
value is data in some rows and a function, a grounded operation, a form or
an expression in others.  A collection's distinct
elements, unique-atom over a collapse of a match, are spread into a bag or
answered, counted or collected, from templates that drop, repeat or fix
variables, some taken apart; a spread reads a variable of the match
outside its scope, and unique-atom is rarely extended by an equation.  A
program the ordinary path cannot run fails.  Every
program runs twice, with CETTA_NATIVE_QUERY_REFERENCE=1 the second time, and
every collection is printed sorted.  Atoms holding variables print with
fresh names in an order that follows them, so the outputs are compared with
every variable named alike and every printed collection's elements sorted.
With --stats, the binary is a runtime-stats build, and the native path must
also have answered at least one collection per program on average, and
joined by rows of bits in at least one program in five, rows it builds for
the join or the pairs of a relation the space keeps as bits, so that the
agreement is not vacuous.
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SYMBOLS = ("a", "b", "c")


def value(rng, nodes):
    return str(rng.randrange(nodes)) if rng.random() < 0.8 else rng.choice(SYMBOLS)


def facts(rng, nodes, rows):
    lines = []
    for _ in range(rows):
        kind = rng.random()
        if kind < 0.55:
            lines.append(f"(edge {value(rng, nodes)} {value(rng, nodes)})")
        elif kind < 0.8:
            lines.append(f"(lab {value(rng, nodes)} {rng.choice(SYMBOLS)} {value(rng, nodes)})")
        else:
            lines.append(f"(mark {value(rng, nodes)})")
    lines += lines[: rng.randrange(4)]
    if rng.random() < 0.15:
        lines.append("(edge $open 1)")
    if rng.random() < 0.15:
        lines.append(f"(mark {rng.randrange(nodes)}.5)")
    return lines


VARIABLES = ("$x", "$y", "$z", "$w")


def binary_facts(rng, nodes):
    """Two binary relations, each row once."""
    rows = set()
    for _ in range(rng.randrange(3, 4 * nodes + 4)):
        head = "link" if rng.random() < 0.3 else "edge"
        rows.add(f"({head} {value(rng, nodes)} {value(rng, nodes)})")
    lines = sorted(rows)
    rng.shuffle(lines)
    return lines


def binary_conjunction(rng, nodes):
    used = set()
    def term():
        if used and rng.random() < 0.6:
            return rng.choice(sorted(used))
        if rng.random() < 0.15:
            return value(rng, nodes)
        fresh = rng.choice(VARIABLES)
        used.add(fresh)
        return fresh
    legs = [f"({'link' if rng.random() < 0.3 else 'edge'} {term()} {term()})"
            for _ in range(rng.randrange(2, 5))]
    text = " ".join(legs)
    bound = sorted(v for v in VARIABLES if v in text)
    return "(, " + text + ")", bound


def leg(rng, nodes, used):
    def term():
        if used and rng.random() < 0.55:
            return rng.choice(sorted(used))
        if rng.random() < 0.2:
            return value(rng, nodes)
        fresh = rng.choice(VARIABLES)
        used.add(fresh)
        return fresh
    kind = rng.random()
    if kind < 0.6:
        return f"(edge {term()} {term()})"
    if kind < 0.85:
        return f"(lab {term()} {rng.choice(SYMBOLS + ('$l',))} {term()})"
    if kind < 0.95:
        return f"(mark {term()})"
    return f"(edge (pair {term()} {term()}) {term()})"


def conjunction(rng, nodes):
    used = set()
    legs = [leg(rng, nodes, used) for _ in range(rng.randrange(1, 5))]
    text = " ".join(legs)
    bound = sorted(v for v in VARIABLES + ("$l",) if v in text)
    pattern = legs[0] if len(legs) == 1 else "(, " + text + ")"
    return pattern, bound


def data_template(rng, bound):
    pick = lambda: rng.choice(bound) if bound else "k"
    shape = rng.random()
    if shape < 0.5:
        return f"(r {pick()} {pick()})"
    if shape < 0.7:
        return f"(r (p {pick()}) {pick()})"
    if shape < 0.85:
        return "k"
    return pick()


def effect(rng, bound):
    space = rng.choice(("&s", "&b", "&self"))
    data = data_template(rng, bound)
    kind = "remove-atom" if rng.random() < 0.4 else "add-atom"
    if rng.random() < 0.3 and bound:
        # rows the match reads: removing them from the matched space changes
        # what later rows of the conjunction read (adding them could make
        # the ordinary run grow without bound)
        data = "(edge " + " ".join(rng.choice(bound) for _ in range(2)) + ")"
        if space == "&self":
            kind = "remove-atom"
    return f"({kind} {space} {data})"


def contained_effect(rng, bound):
    """An effect that never adds a bare variable to the matched space: bound
    to an open row's variable, it would be a row every later pattern reads,
    and the ordinary run would grow without bound."""
    made = effect(rng, bound)
    if made.startswith("(add-atom &self $"):
        made = f"(add-atom &self (r {made[len('(add-atom &self '):-1]} k))"
    return made


def effects(rng, bound):
    shape = rng.random()
    if shape < 0.55:
        return effect(rng, bound)
    body = rng.choice(("ok", f"(done {rng.choice(bound) if bound else 'k'})"))
    if shape < 0.7:
        return f"(let $u {effect(rng, bound)} {body})"
    binders = " ".join(f"($e{i} {effect(rng, bound)})" for i in range(rng.randrange(2, 4)))
    return f"(let* ({binders}) {body})"


def program(rng):
    nodes = rng.randrange(2, 7)
    binary = rng.random() < 0.35
    lines = (binary_facts(rng, nodes) if binary
             else facts(rng, nodes, rng.randrange(3, 16)))
    lines += ["!(bind! &s (new-space set))", "!(bind! &b (new-space))"]
    if rng.random() < 0.35:
        # pairs a later query swaps: each row removes one and adds its
        # mirror, which another row may have removed
        space = rng.choice(("&s", "&b"))
        lines.append(f"!(let $h (collapse (match &self (edge $x $y) (add-atom {space} (r $x $y)))) done)")
        lines.append(f"!(let $h (collapse (match &self (edge $x $y) (let* (($a (remove-atom {space} (r $x $y))) ($b (add-atom {space} (r $y $x)))) ok))) (size-atom $h))")
    for index in range(rng.randrange(2, 6)):
        pattern, bound = (binary_conjunction(rng, nodes)
                          if binary and rng.random() < 0.8
                          else conjunction(rng, nodes))
        target = rng.random() < 0.6
        template = effects(rng, bound) if target else data_template(rng, bound)
        match = f"(match &self {pattern} {template})"
        observer = rng.random()
        if observer < 0.2 and target:
            lines.append(f"(= (rule{index}) {match})")
            lines.append(f"!(let $h (collapse (rule{index})) (size-atom $h))")
        elif observer < 0.4:
            # a collection inside a function's body
            lines.append(f"(= (query{index}) (let $h (collapse {match}) (sort-atom $h)))")
            lines.append(f"!(query{index})")
        elif observer < 0.55:
            lines.append(f"!(size-atom (collapse {match}))")
        elif observer < 0.65:
            lines.append(f"!(let $h (collapse {match}) done)")
        else:
            lines.append(f"!(let $h (collapse {match}) (sort-atom $h))")
    for index in range(rng.randrange(0, 3)):
        # effects over a list: each element its pattern meets makes them
        names = list(VARIABLES[:2])
        if rng.random() < 0.6:
            pattern, bound = conjunction(rng, nodes)
            names = bound[:2] if len(bound) >= 2 else names
            source = f"(collapse (match &self {pattern} (pair {names[0]} {names[1]})))"
        else:
            items = [f"(pair {value(rng, nodes)} {value(rng, nodes)})"
                     for _ in range(rng.randrange(1, 6))]
            items += items[: rng.randrange(3)]
            if rng.random() < 0.4:
                items.append(f"(other {value(rng, nodes)})")
            rng.shuffle(items)
            source = "(" + " ".join(items) + ")"
        meets = f"(pair {names[0]} {names[1]})"
        if rng.random() < 0.25:
            meets = f"(pair {names[0]} {value(rng, nodes)})"
        body = effects(rng, names)
        if rng.random() < 0.3:
            # a variable meets each element whole
            meets = names[0]
            body = effects(rng, names[:1])
        lines.append(f"!(let $l {source} (size-atom (collapse (let {meets} (superpose $l) {body}))))")
    # Rows read one way: constructor patterns over ground cells, with a
    # variable shared between columns, repeated, nested, or left open.  Their
    # own relations and random stream leave the programs above as they were.
    extra = random.Random(nodes * 7919 + len(lines))
    if extra.random() < 0.4:
        n = extra.randrange(nodes)
        lines += [f"(cell (p {n}))", f"(cell (p (p {n})))", "(cell (q a))",
                  f"(cell {n})"]
        lines += extra.sample(["(step (p $u) (q $u))", "(step (p (p $u)) $u)",
                               "(step (p $u) (p $u))", "(step (q $u) $w)",
                               "(step (p $u) $u)", "(step $u (p $u))"],
                              extra.randrange(1, 4))
        lines.append("!(let $h (collapse (match &self (, (cell $x) (step $x $y)) (r $x $y))) (sort-atom $h))")
        lines.append("!(size-atom (collapse (match &self (, (cell $x) (step $x $y)) t)))")
        lines.append("!(size-atom (collapse (match &self (, (cell $x) (step $x $y) (step $y $z)) t)))")
    # Let chains whose bodies are themselves effects, with the match's
    # variables or ground.  Their own random stream leaves the programs
    # above as they were.
    bodies = random.Random(nodes * 15485863 + len(lines))
    for _ in range(bodies.randrange(0, 3)):
        pattern, bound = conjunction(bodies, nodes)
        body = contained_effect(bodies, bound if bodies.random() < 0.6 else [])
        if bodies.random() < 0.5:
            template = f"(let $u {contained_effect(bodies, bound)} {body})"
        else:
            binders = " ".join(f"($e{i} {contained_effect(bodies, bound)})"
                               for i in range(bodies.randrange(2, 4)))
            template = f"(let* ({binders}) {body})"
        lines.append(f"!(let $h (collapse (match &self {pattern} {template})) (sort-atom $h))")
    # Rows whose first column is a term, and patterns holding variables
    # inside such terms: a variable repeated within a term or across
    # patterns, a part the pattern fixes, a pair of pairs; among the rows,
    # one holding a variable and one holding a float inside a pair.  Their
    # own relation and random stream leave the programs above as they were.
    nested = random.Random(nodes * 104729 + len(lines))
    if nested.random() < 0.6:
        for _ in range(nested.randrange(2, 9)):
            inner = f"(pair {value(nested, nodes)} {value(nested, nodes)})"
            if nested.random() < 0.3:
                inner = f"(pair {inner} {value(nested, nodes)})"
            lines.append(f"(nest {inner} {value(nested, nodes)})")
        if nested.random() < 0.2:
            lines.append("(nest (pair $open 2) 3)")
        if nested.random() < 0.2:
            lines.append("(nest (pair 1.5 2) 3)")
        for _ in range(nested.randrange(1, 4)):
            first, second = nested.choice(VARIABLES), nested.choice(VARIABLES)
            shape = nested.random()
            if shape < 0.35:
                second = first
            elif shape < 0.6:
                first = value(nested, nodes)
            inner = f"(pair {first} {second})"
            if nested.random() < 0.3:
                inner = f"(pair {inner} {nested.choice(VARIABLES)})"
            legs = [f"(nest {inner} {nested.choice(VARIABLES)})"]
            if nested.random() < 0.4:
                legs.append(f"(edge {nested.choice(VARIABLES)} {nested.choice(VARIABLES)})")
            text = " ".join(legs)
            pattern = legs[0] if len(legs) == 1 else "(, " + text + ")"
            bound = sorted(v for v in VARIABLES if v in text)
            template = (contained_effect(nested, bound) if nested.random() < 0.4
                        else data_template(nested, bound))
            lines.append(f"!(let $h (collapse (match &self {pattern} {template})) (sort-atom $h))")
    # Matches inside matches, over the matched space, a bag or a set,
    # sharing variables: the native path joins them as one conjunction, each
    # pattern reading its own space, unless an effect may change a row one
    # reads.  Their own random stream leaves the programs above as they were.
    inner = random.Random(nodes * 1299709 + len(lines))
    if inner.random() < 0.6:
        for _ in range(inner.randrange(2, 7)):
            space = inner.choice(("&b", "&s"))
            lines.append(f"!(add-atom {space} (r {value(inner, nodes)} {value(inner, nodes)}))")
        for _ in range(inner.randrange(1, 4)):
            outer, bound = conjunction(inner, nodes)
            levels = []
            for _ in range(inner.randrange(1, 3)):
                space = inner.choice(("&self", "&b", "&s"))
                first = inner.choice(bound) if bound and inner.random() < 0.8 \
                    else value(inner, nodes)
                second = inner.choice(VARIABLES)
                head = "edge" if space == "&self" else "r"
                levels.append((space, f"({head} {first} {second})"))
                bound = sorted(set(bound) | {second})
            template = (contained_effect(inner, bound) if inner.random() < 0.5
                        else data_template(inner, bound))
            for space, pattern in reversed(levels):
                template = f"(match {space} {pattern} {template})"
            match = f"(match &self {outer} {template})"
            if inner.random() < 0.3:
                lines.append(f"!(size-atom (collapse {match}))")
            else:
                lines.append(f"!(let $h (collapse {match}) (sort-atom $h))")
    # A row holding a variable that a removal takes though the removal and
    # a pattern fix different atoms: (br $z) meets both (br 7), which a row
    # removes, and (br 5), which the next row reads.  Their own random
    # stream leaves the programs above as they were.
    bridge = random.Random(nodes * 2750159 + len(lines))
    if bridge.random() < 0.5:
        for _ in range(bridge.randrange(1, 4)):
            nested_form = bridge.random() < 0.5
            space = bridge.choice(("&self", "&b")) if nested_form else "&self"
            rows = ["(br $z)", f"(br {bridge.randrange(nodes + 2)})"]
            if bridge.random() < 0.3:
                rows.append("(br (pair $z 1))")
            lines += [f"!(add-atom {space} {row})" for row in rows]
            read, removed = bridge.sample(range(nodes + 2), 2)
            effect = f"(remove-atom {space} (br {removed}))"
            if nested_form:
                match = f"(match &self (edge $x $y) (match {space} (br {read}) {effect}))"
            else:
                match = f"(match &self (, (edge $x $y) (br {read})) {effect})"
            lines.append(f"!(let $h (collapse {match}) (sort-atom $h))")
    # Calls of a function of one equation that branches on an absence,
    # inside matches: a native join with a guard, unless an effect may
    # change a row the guard reads, the function has another equation or a
    # declared type, the guard reads a variable no pattern binds, or an
    # instance added would be a rule.  Their own random stream leaves the
    # programs above as they were.
    guarded = random.Random(nodes * 3010349 + len(lines))
    # rarely, an equation extends `==`, which PeTTa's extended profile
    # answers beside the builtin: no test reading `==` may then be read
    # natively
    if random.Random(nodes * 7 + len(lines)).random() < 0.15:
        lines.append("(= (== $a $b) maybe)")
        lines.append("!(bind! &e (new-space set))")
        lines.append("!(add-atom &e (mark 1))")
        lines.append("(= (keepext $x $y $T) (if (== (collapse (match &e (mark $y) t)) ()) "
                     "(add-atom $T (got $x $y)) done))")
        lines.append("!(size-atom (collapse (match &self (edge $x $y) (keepext $x $y &b))))")
    if guarded.random() < 0.6:
        lines.append("!(bind! &m (new-space set))")
        # a bag holding a guard's row twice, which a case on its match
        # answers twice
        lines.append("!(bind! &bg (new-space))")
        for v in guarded.sample(range(nodes), guarded.randrange(1, nodes)):
            lines += [f"!(add-atom &bg (mark {v}))"] * guarded.choice((1, 2))
        for v in guarded.sample(range(nodes), guarded.randrange(1, nodes)):
            space = guarded.choice(("&self", "&m"))
            row = f"(mark {v})"
            lines.append(row if space == "&self" else f"!(add-atom &m {row})")
        if guarded.random() < 0.25:
            lines.append("(edge 0 (= (f $q) $q))")
        # rows holding variables where a guard reads: they meet the guard's
        # instances by unification, which no exact probe sees
        for _ in range(guarded.randrange(0, 3)):
            open_row = guarded.choice(("(mark $any)", "(mark (pair $p 1))",
                                       f"(mark (got {guarded.randrange(nodes)} $q))"))
            space = guarded.choice(("&self", "&m", "&s"))
            lines.append(open_row if space == "&self"
                         else f"!(add-atom {space} {open_row})")
        for index in range(guarded.randrange(1, 4)):
            name = f"keep{index}"
            guard_space = guarded.choice(("&self", "&m", "&s", "&bg"))
            shape = guarded.random()
            data = "$y" if shape < 0.45 else "(got $x $y)"
            if shape > 0.9:
                data = "(mark $y)"  # meets the guard when it writes there
            read = guarded.choice(("$x", "$y"))
            if guarded.random() < 0.1:
                read = "$free"  # a variable no pattern binds
            otherwise = guarded.choice(("done", "0", "skip"))
            # the test in one of PeTTa's spellings of an empty collection;
            # the last reads the collection in a branch too, which no
            # rewrite may take
            collected = f"(collapse (match {guard_space} (mark {read}) t))"
            then = f"(add-atom $T {data})"
            spelling = random.Random(nodes * 92821 + index * 31 + len(lines))
            form = spelling.randrange(13)
            if form == 0:
                body = f"(if (== {collected} ()) {then} {otherwise})"
            elif form == 1:
                body = f"(if (== () {collected}) {then} {otherwise})"
            elif form == 2:
                body = f"(if (== (size-atom {collected}) 0) {then} {otherwise})"
            elif form == 3:
                body = f"(if (!= {collected} ()) {otherwise} {then})"
            elif form == 4:
                body = f"(if (> (size-atom {collected}) 0) {otherwise} {then})"
            elif form == 5:
                body = f"(case {collected} ((() {then}) ($_ {otherwise})))"
            elif form == 6:
                body = f"(let $c {collected} (if (== $c ()) {then} {otherwise}))"
            elif form == 7:
                lines.append("(= (none-in $s $p) (== (collapse (match $s $p t)) ()))")
                body = f"(if (none-in {guard_space} (mark {read})) {then} {otherwise})"
            elif form == 8:
                body = f"(let $c {collected} (if (== $c ()) {then} (seen $c)))"
            elif form == 9:
                once = f"(once (match {guard_space} (mark {read}) t))"
                body = f"(case {once} ((Empty {then}) ($v {otherwise})))"
            elif form == 11:
                key = f"(match {guard_space} (mark {read}) t)"
                body = f"(case {key} ((Empty {then}) ($_ {otherwise})))"
            elif form == 12:
                key = f"(match {guard_space} (mark {read}) t)"
                body = f"(case {key} (($w {otherwise}) (Empty {then})))"
            else:
                once = f"(once (match {guard_space} (mark {read}) {read}))"
                body = f"(case {once} ((Empty {then}) ($v (seen $v))))"
            lines.append(f"(= ({name} $x $y $T) {body})")
            if guarded.random() < 0.12:
                lines.append(f"(= ({name} $x $y $T) also)")
            if guarded.random() < 0.08:
                lines.append(f"(: {name} (-> Atom Atom Atom Atom))")
            target = guarded.choice(("&s", "&b", "&m", guard_space))
            match = f"(match &self (edge $x $y) ({name} $x $y {target}))"
            if guarded.random() < 0.3:
                match = (f"(match &self (edge $x $z) (match &self (edge $z $y) "
                         f"({name} $x $y {target})))")
            if guarded.random() < 0.8:
                lines.append(f"!(size-atom (collapse {match}))")
            else:
                lines.append(f"!(let $h (collapse {match}) (sort-atom $h))")
        # a case on a match over a bag holding every probed row twice,
        # which answers its default twice for each
        lines.append("!(bind! &bd (new-space))")
        for v in range(nodes):
            lines += [f"!(add-atom &bd (mark {v}))"] * 2
        lines.append("(= (keepbd $x $y $T) (case (match &bd (mark $y) t) "
                     "((Empty (add-atom $T (got $x $y))) ($_ done))))")
        lines.append("!(size-atom (collapse (match &self (edge $x $y) (keepbd $x $y &s))))")
        # a guard over the bag its own branch writes, meeting the row it
        # adds: on the ordinary path a later solution sees the row an
        # earlier one added, so each target is added once
        lines.append("!(bind! &g (new-space))")
        lines.append("(= (firstseen $x $y) (if (== (collapse (match &g (seen $y) t)) ()) "
                     "(add-atom &g (seen $y)) done))")
        lines.append("!(size-atom (collapse (match &self (edge $x $z) (match &self "
                     "(edge $z $y) (firstseen $x $y)))))")
        lines.append("!(let $h (collapse (get-atoms &g)) (sort-atom $h))")
        if guarded.random() < 0.5:
            # a guard over a space holding a row with a variable under the
            # guard's head, the effect writing elsewhere: the row meets every
            # instance by unification
            open_row = guarded.choice(("(mark $any)", "(mark (pair $p 1))",
                                       f"(mark (got {guarded.randrange(nodes)} $q))"))
            lines.append(f"!(add-atom &m {open_row})")
            read = guarded.choice(("$x", "$y"))
            lines.append(f"(= (keepopen $x $y $T) (if (== (collapse (match &m "
                         f"(mark {read}) t)) ()) (add-atom $T (got $x $y)) done))")
            lines.append("!(size-atom (collapse (match &self (edge $x $y) "
                         "(keepopen $x $y &b))))")
        lines.append("!(let $h (collapse (get-atoms &m)) (sort-atom $h))")
    # Directives that are matches, or calls of a function of one equation
    # whose body is one: a directive's answers are all taken before any
    # prints, so the native path may collect them.  Their own random
    # stream leaves the programs above as they were.
    directive = random.Random(nodes * 7919 + len(lines))
    if directive.random() < 0.7:
        for index in range(directive.randrange(1, 4)):
            shape = directive.random()
            join = directive.choice((
                "(, (edge $x $y) (edge $y $z))",
                "(edge $x $y)",
                "(, (edge $x $y) (edge $y $x))"))
            nested = directive.random() < 0.3
            if shape < 0.35:
                answer = directive.choice(("(pair $x $y)", "$y", "(one $x)"))
                body = answer
            else:
                target = directive.choice(("&s", "&b"))
                data = directive.choice(("(two $x $y)", "(path $x)", "(open $x $w)"))
                body = f"(add-atom {target} {data})"
            if nested:
                match = f"(match &self (edge $x $y) (match &self (edge $y $z) {body}))"
            else:
                match = f"(match &self {join} {body})"
            if shape > 0.8 and body.startswith("(add-atom"):
                lines.append(f"(= (directive{index}) {match})")
                match = f"(directive{index})"
            lines.append("!(let $m seg-begin $m)")
            lines.append(f"!{match}")
            lines.append("!(let $m seg-end $m)")
    # Folds over matches: a step that ignores its item folds the number of
    # answers, so a generator adding or removing rows may run natively; a
    # step reading its item folds the template's values.  Lambdas, builtin
    # operations and remainders, by literals or by what may be zero.  A
    # template that is a call of two answers gives two items a solution, and
    # so does a row holding such a call, which the fold evaluates again.
    fold = random.Random(nodes * 104729 + len(lines))
    if fold.random() < 0.6:
        lines.append("(= (fi $v) (superpose ($v $v)))")
        # rows holding a call, which the fold's evaluation of each answer runs
        lines += ["(= (fz $n) (superpose (a b)))", "(ec (fz 1))", "(ec plain)", "(ec 7)"]
        lines.append("!(foldall (|-> ($i $acc) (+ $acc 1)) (match &self (ec $c) $c) 0)")
        lines.append("!(foldall (|-> ($i $acc) (+ $acc 1)) (match &self (, (ec $c) (edge $x $y)) $c) 0)")
        for _ in range(fold.randrange(1, 4)):
            step = fold.choice((
                "(|-> ($i $acc) (+ $acc 1))", "(|-> ($i $acc) (* $acc 2))",
                "(|-> ($i $acc) (% (+ $acc 3) 7))", "(|-> ($i $acc) (- $acc 2))",
                "(|-> ($i $acc) (% $acc $acc))", "(|-> ($i $acc) (+ $acc $i))",
                "+", "max"))
            join = fold.choice(("(edge $x $y)", "(, (edge $x $y) (edge $y $z))"))
            each = fold.choice((
                "(add-atom &b (fold $x))", "(add-atom &s (fold $y))",
                "(remove-atom &b (fold $x))", "$y", "2", "(fi $x)"))
            initial = fold.choice(("0", "1", "5"))
            lines.append(f"!(foldall {step} (match &self {join} {each}) {initial})")
    # One region, several spellings: a call of a function of one equation
    # whose body holds variables of its own, a let-wrapped join, let-bound
    # data, an effect helper; under every consumer.  Controls the normal form
    # must leave to the machine: a second equation, a declared type, and
    # arguments a call computes, which PeTTa evaluates before the call.
    spell = random.Random(nodes * 15485863 + len(lines))
    if spell.random() < 0.7:
        lines.append("(= (nullary) 3)")
        lines.append("(= konst 5)")
        for index in range(spell.randrange(1, 4)):
            name = f"region{index}"
            shape = spell.random()
            target = spell.choice(("&s", "&b"))
            if shape < 0.25:
                lines.append(f"(= ({name} $k) (match &self (, (edge $k $y) (edge $y $z)) "
                             f"(add-atom {target} (reach $k $z))))")
            elif shape < 0.5:
                lines.append(f"(= ({name} $k) (let $t {target} (match &self (edge $k $y) "
                             f"(add-atom $t (step $k $y)))))")
            elif shape < 0.75:
                lines.append(f"(= ({name} $k) (match &self (edge $k $y) "
                             f"(let $d (pair $k (wrap $y)) (add-atom {target} $d))))")
            else:
                lines.append(f"(= (put{index} $a $b) (add-atom {target} (put $a $b)))")
                lines.append(f"(= ({name} $k) (match &self (edge $k $y) (put{index} $k $y)))")
            control = spell.random()
            if control < 0.15:
                lines.append(f"(= ({name} $k) other)")
            elif control < 0.3:
                lines.append(f"(: {name} (-> Atom Atom))")
            argument = spell.choice(("0", "1", str(spell.randrange(nodes)), "(nullary)",
                                     "konst", "(+ 0 1)", "(wrap 2)", "$free", "$y"))
            call = f"({name} {argument})"
            consumer = spell.choice(("count", "collect", "directive", "empty"))
            if consumer == "count":
                lines.append(f"!(size-atom (collapse {call}))")
            elif consumer == "collect":
                lines.append(f"!(let $h (collapse {call}) (sort-atom $h))")
            elif consumer == "directive":
                lines.append("!(let $m seg-begin $m)")
                lines.append(f"!{call}")
                lines.append("!(let $m seg-end $m)")
            else:
                lines.append(f"!(if (== (collapse {call}) ()) none some)")
    # Effects adding variable data outside guards: each instance is checked
    # against PeTTa's rule and declaration heads, so a row holding an
    # equation or a declaration makes the native path decline; and the
    # collect-then-superpose spelling, whose list is the snapshot.
    loose = random.Random(nodes * 130003 + len(lines))
    if loose.random() < 0.5:
        if loose.random() < 0.3:
            lines.append(loose.choice(("(edge 1 (= (g $q) $q))", "(edge 2 (: h Number))")))
        target = loose.choice(("&s", "&b"))
        lines.append(f"!(size-atom (collapse (match &self (edge $x $y) (add-atom {target} $y))))")
        lines.append(f"!(let $ps (collapse (match &self (, (edge $x $y) (edge $y $z)) (hop $x $z))) "
                     f"(size-atom (collapse (let $a (superpose $ps) (add-atom {target} $a)))))")
    # Collect-then-superpose: every read of the match happens before any
    # effect, so effects may change the very rows the match reads, which a
    # native snapshot region must read as the list does; in a directive and
    # in an equation's body, collected, counted and streamed; and a list
    # read twice, which no region may take.
    snap = random.Random(nodes * 2750159 + len(lines))
    if snap.random() < 0.6:
        lines.append("!(bind! &q (new-space))")
        for v in snap.sample(range(nodes), min(nodes, snap.randrange(2, 5))):
            lines.append(f"!(add-atom &q (q {v}))")
        for index in range(snap.randrange(1, 4)):
            form = snap.randrange(6)
            if form == 0:
                region = ("(let $ps (collapse (match &q (q $x) (q $x))) "
                          "(size-atom (collapse (let $a (superpose $ps) (add-atom &q (q (s $a)))))))")
            elif form == 1:
                region = ("(let $ps (collapse (match &q (q $x) $x)) "
                          "(collapse (let $a (superpose $ps) (remove-atom &q (q $a)))))")
            elif form == 2:
                region = ("(let $ps (collapse (match &self (edge $x $y) (pair $x $y))) "
                          "(size-atom (collapse (let (pair $u $v) (superpose $ps) (add-atom &q (q $u))))))")
            elif form == 3:
                region = ("(let $ps (collapse (match &q (q $x) (w $x))) "
                          "(let $a (superpose $ps) (add-atom &q $a)))")
            elif form == 4:
                region = ("(let $ps (collapse (match &q (q $x) $x)) "
                          "(collapse (let $a (superpose $ps) (pair $a $ps))))")
            else:
                region = ("(let $ps (collapse (match &self (edge $x $y) $y)) "
                          "(size-atom (collapse (let $a (superpose $ps) (add-atom &q (q $a))))))")
            if snap.random() < 0.5:
                lines.append(f"(= (snap{index}) {region})")
                lines.append(f"!(let $h (collapse (snap{index})) (sort-atom $h))")
            else:
                lines.append("!(let $m seg-begin $m)")
                lines.append(f"!{region}")
                lines.append("!(let $m seg-end $m)")
        lines.append("!(let $h (collapse (get-atoms &q)) (sort-atom $h))")
    # Let chains: nested lets of effects, which the order of a removal and
    # an addition of the same row makes observable, a let* holding a let,
    # integer arithmetic a let binds before a match (and a float and an
    # overflow it must not fold), and a listed query's chain.
    chains = random.Random(nodes * 15485867 + len(lines))
    if chains.random() < 0.6:
        target = chains.choice(("&s", "&b"))
        for _ in range(chains.randrange(1, 4)):
            form = chains.randrange(6)
            if form == 0:
                body = (f"(let $a (add-atom {target} (n1 $x)) "
                        f"(let $c (add-atom {target} (n2 $y)) done))")
            elif form == 1:
                body = (f"(let $a (remove-atom {target} (n1 $x)) "
                        f"(let $c (add-atom {target} (n1 $x)) t))")
            elif form == 2:
                body = (f"(let* (($a (add-atom {target} (n3 $y)))) "
                        f"(let $c (remove-atom {target} (n3 $y)) t))")
            elif form == 3:
                body = (f"(let $a (add-atom {target} (n4 $x)) "
                        f"(let* (($b (add-atom {target} (n5 $y))) ($c (remove-atom {target} (n4 $x)))) t))")
            elif form == 4:
                k = chains.choice(("2", "(+ 2 1)", "1.5", "9223372036854775807"))
                lines.append(f"(= (lvl $k) (let $k1 (+ $k 1) (match &self (edge $k $y) "
                             f"(add-atom {target} (lv $k1 $y)))))")
                lines.append(f"!(size-atom (collapse (lvl {k})))")
                continue
            else:
                lines.append(f"!(let $ps (collapse (match &self (edge $x $y) (two $x $y))) "
                             f"(size-atom (collapse (let (two $u $v) (superpose $ps) "
                             f"(let $a (add-atom {target} (u $u)) (let $c (add-atom {target} (v $v)) t))))))")
                continue
            lines.append(f"!(size-atom (collapse (match &self (edge $x $y) {body})))")
    # Destructuring a matched variable: `(let P $x B)` or `(case $x ((P B)
    # ...))` over a variable the pattern binds, which reads P in the
    # pattern when $x occurs nowhere else; collected, counted, and inside an
    # equation's body.  Controls: $x read again in B, a default that answers,
    # P sharing a variable with the pattern, rows holding variables.
    shapes = random.Random(nodes * 104659 + len(lines))
    if shapes.random() < 0.6:
        for i in range(shapes.randrange(3, 8)):
            a, b = shapes.randrange(3), shapes.randrange(3)
            lines.append(f"(cell (p {a} {b}) {shapes.randrange(nodes)})")
        if shapes.random() < 0.4:
            lines.append(f"(cell (p $z {shapes.randrange(3)}) {shapes.randrange(nodes)})")
        for index in range(shapes.randrange(1, 4)):
            form = shapes.randrange(7)
            if form == 0:
                body = "(let (p $a $b) $f (got $a $b))"
            elif form == 1:
                body = "(case $f (((p $a 1) (got $a $n)) ($_ (empty))))"
            elif form == 2:
                body = "(case $f (((p $a $b) (got $b $a))))"
            elif form == 3:
                body = "(let (p $a $b) $f (got $f $a))"
            elif form == 4:
                body = "(case $f (((p $a 2) (got $a)) ($_ other)))"
            elif form == 5:
                body = "(let (p $n $b) $f (got $n $b))"
            else:
                body = "(let (p $a $b) $f (add-atom &b (got $a $b $n)))"
            match = f"(match &self (cell $f $n) {body})"
            consumer = shapes.randrange(3)
            if consumer == 0:
                lines.append(f"!(let $h (collapse {match}) (sort-atom $h))")
            elif consumer == 1:
                lines.append(f"!(size-atom (collapse {match}))")
            else:
                lines.append(f"(= (look{index} $n) (match &self (cell $f $n) {body}))")
                lines.append(f"!(let $h (collapse (look{index} {shapes.randrange(nodes)})) (sort-atom $h))")
    # Scopes: a variable a rewrite would bind elsewhere, or not at all, read
    # outside the construct that binds it: a let's or a case clause's
    # variable that the pattern binds first or an if branch reads, a let's
    # variable read after the let or passed in by a caller, a collected
    # match's variables read by what spreads the list, an equation's
    # parameter in the collected match, ground or not; and calls of typed
    # functions whose collection nothing reads, or something does.
    scopes = random.Random(zlib.crc32("\n".join(lines).encode()))
    if scopes.random() < 0.7:
        lines += ["(zz ())", "(zz (k))", "(zz k)", "(zz 1)", "(ww (y 1))", "(ww (y 2))",
                  f"(edge a {scopes.randrange(nodes)})"]
        for index in range(scopes.randrange(3, 7)):
            n = scopes.randrange(nodes)
            form = scopes.randrange(15)
            if form == 0:
                v = scopes.choice((str(n), f"(+ {n} 1)", "(y 1)"))
                lines.append(f"!(let $h (collapse (match &self (edge $x $y) (let $y {v} (hit $x $y)))) (sort-atom $h))")
                lines.append("!(let $h (collapse (match &self (ww $v) (let $v (y 1) (k $v)))) (sort-atom $h))")
            elif form == 1:
                c = scopes.choice(("$c", "$d"))
                lines.append(f"!(let $h (collapse (match &self (zz {c}) (let $c (collapse (match &self (edge {n} $q) $q)) "
                             f"(if (== $c ()) (none {c}) (some {c}))))) (sort-atom $h))")
            elif form == 2:
                o = scopes.choice(("$o", "$p"))
                lines.append(f"!(let $h (collapse (match &self (zz {o}) (case (collapse (match &self (edge {n} $q) $q)) "
                             f"((() (e {o})) ($o (n $o)))))) (sort-atom $h))")
            elif form == 3:
                o = scopes.choice(("$o", "$p"))
                lines.append(f"!(let $h (collapse (match &self (zz {o}) (case (once (match &self (edge {n} $q) $q)) "
                             f"((Empty (e {o})) ($o (n $o)))))) (sort-atom $h))")
            elif form == 4:
                lines.append("!(let $h (collapse (match &self (edge $x $y) (if (let $k $y (== (collapse (match &self (edge $k $q) $q)) ())) "
                             "(leaf $k) (inner $k)))) (sort-atom $h))")
            elif form == 5:
                lines.append("!(let $_ (let $r (collapse (match &self (edge $x $y) $x)) (size-atom $r)) (sort-atom $r))")
                lines.append("!(let $_ (let $L (collapse (match &self (edge $x $y) (p $x))) "
                             "(collapse (let (p $v) (superpose $L) (q $v)))) (sort-atom $L))")
            elif form == 6:
                lines.append(f"(= (gs{index} $L) (let $L (collapse (match &self (edge $x $y) (p $x))) "
                             f"(collapse (let (p $v) (superpose $L) (q $v)))))")
                lines.append(f"!(let $_ (gs{index} $z) (sort-atom $z))")
                lines.append(f"!(let $h (gs{index} $z) (sort-atom $h))")
            elif form == 7:
                lines.append("!(let $L (collapse (match &self (edge $x $y) $x)) (collapse (let $v (superpose $L) (pair $v $y))))")
                lines.append("!(let $L (collapse (match &self (edge $x $y) $x)) "
                             "(size-atom (collapse (let $v (superpose $L) (match &self (edge $q $y) $q)))))")
                lines.append("!(let $_ (let $L (collapse (match &self (edge $x $y) $x)) (let $v (superpose $L) $v)) $y)")
                lines.append("!(let $L (collapse (match &self (edge $x $y) (p $x))) (collapse (let $v (superpose $L) (pair $v $x))))")
                # the same regions as a collection's own query
                lines.append("!(let $h (collapse (let $L (collapse (match &self (edge $x $y) $x)) "
                             "(let $v (superpose $L) (pair $v $y)))) (sort-atom $h))")
                lines.append("!(let $h (collapse (let $L (collapse (match &self (edge $x $y) (p $x))) "
                             "(let $v (superpose $L) (pair $v $L)))) (size-atom $h))")
            elif form == 8:
                lines.append(f"(= (gp{index} $k) (let $L (collapse (match &self (edge $k $y) $y)) "
                             f"(collapse (let $v (superpose $L) (to $v)))))")
                lines.append(f"!(let $h (gp{index} {n}) (sort-atom $h))")
                lines.append(f"!(let $h (gp{index} $u) (sort-atom $h))")
                lines.append(f"!(let $_ (gp{index} $u) $u)")
                lines.append(f"(= (gq{index} $k) (let $L (collapse (match &self (edge $k $y) $y)) "
                             f"(let $v (superpose $L) (to $v))))")
                lines.append(f"!(let $_ (gq{index} $u) $u)")
                lines.append(f"!(let $h (collapse (gq{index} {n})) (sort-atom $h))")
                # one caller variable for a parameter the match reads and one
                # the spread reads
                lines.append(f"(= (gr{index} $k $m) (let $L (collapse (match &self (edge $k $y) $y)) "
                             f"(collapse (let $v (superpose $L) (to $v $m)))))")
                lines.append(f"!(let $h (gr{index} $u $u) (sort-atom $h))")
                lines.append(f"!(let $h (gr{index} {n} $w) (sort-atom $h))")
            elif form == 14:
                # the list's own variable spreads it again: bound, it meets
                # no element
                lines.append("!(let $ps (collapse (match &self (edge $x $y) (w $x))) "
                             "(size-atom (collapse (let $ps (superpose $ps) (add-atom &b $ps)))))")
                lines.append(f"(= (sp{index}) (let $ps (collapse (match &self (edge $x $y) (w $y))) "
                             f"(collapse (let $ps (superpose $ps) (add-atom &s $ps)))))")
                lines.append(f"!(let $h (collapse (sp{index})) (sort-atom $h))")
            else:
                result = scopes.choice(("Expression", "%Undefined%", "Atom", "Number", "Bool", "Symbol"))
                target = scopes.choice(("&s", "&b"))
                if form == 9:
                    lines.append(f"(: tf{index} (-> {result}))")
                    if scopes.random() < 0.2:
                        lines.append(f"(: tf{index} (-> {scopes.choice(('Expression', '%Undefined%'))}))")
                    lines.append(f"(= (tf{index}) (match &self (edge $x $y) (add-atom {target} (tf{index} $x $y))))")
                    call = f"(tf{index})"
                elif form == 10:
                    domain = scopes.choice(("Number", "%Undefined%", "Atom", "Symbol"))
                    lines.append(f"(: tg{index} (-> {domain} {result}))")
                    lines.append(f"(= (tg{index} $k) (match &self (edge $k $y) (add-atom {target} (tg{index} $k $y))))")
                    call = f"(tg{index} {scopes.choice((str(n), 'a'))})"
                    for argument in (str(n), "a"):
                        lines.append(f"!(let $r (collapse (tg{index} {argument})) done)")
                else:
                    lines.append(f"(: tf{index} (-> {result}))")
                    lines.append(f"(= (tf{index}) (match &self (edge $x $y) (add-atom {target} (tf{index} $x $y))))")
                    call = f"(tf{index})"
                consumer = scopes.randrange(6)
                if consumer == 0:
                    lines.append(f"!(let $r (collapse {call}) done)")
                elif consumer == 1:
                    lines.append(f"!(let $r (collapse {call}) (size-atom $r))")
                elif consumer == 2:
                    lines.append(f"!(let $_ (let $r (collapse {call}) done) (sort-atom $r))")
                elif consumer == 3:
                    lines.append(f"!(let $r (k) (let $r (collapse {call}) done))")
                elif consumer == 4:
                    lines.append(f"(= (runner{index}) (let $r (collapse {call}) done))")
                    lines.append(f"!(runner{index})")
                else:
                    lines.append(f"(= (runner{index} $r) (let $r (collapse {call}) done))")
                    lines.append(f"!(let $_ (runner{index} $q) (sort-atom $q))")
    # Dispatch: a function of several equations whose heads hold atoms,
    # called with one of them, with none, with a variable, and with a float
    # beside an integer; some also with a head holding a variable, which
    # meets every call.
    dispatch = random.Random(zlib.crc32(("dispatch\n" + "\n".join(lines)).encode()))
    if dispatch.random() < 0.6:
        for index in range(dispatch.randrange(1, 4)):
            target = dispatch.choice(("&s", "&b"))
            keys = dispatch.sample(["a", "b", "c", "1", "2", '"s"'], 3)
            for key in keys[:2]:
                lines.append(f"(= (dp{index} {key}) (match &self (edge $x $y) "
                             f"(add-atom {target} (dp{index} {key} $x))))")
            if dispatch.random() < 0.35:
                lines.append(f"(= (dp{index} $v) (match &self (edge $v $y) (dpv{index} $v $y)))")
            if dispatch.random() < 0.25:
                lines.append(f"(= (dp{index} 1.0) (match &self (edge $x $y) (dpf{index} $x)))")
            for key in keys + ["$u", "1.0"]:
                call = f"(dp{index} {key})"
                consumer = dispatch.randrange(3)
                if consumer == 0:
                    lines.append(f"!(let $r (collapse {call}) done)")
                elif consumer == 1:
                    lines.append(f"!(let $h (collapse {call}) (size-atom $h))")
                else:
                    lines.append(f"!(let $h (collapse {call}) (sort-atom $h))")
    # Whether a match has a row, in every spelling: before adding the row
    # into a set or a bag, inside an equation and at top level, compared
    # with () with and without once, counted against 0, by == and by !=;
    # and controls, a count compared with 1 and a template holding a
    # variable, which no observation of existence may read.
    exists = random.Random(zlib.crc32(("exists\n" + "\n".join(lines)).encode()))
    if exists.random() < 0.6:
        spellings = [
            "(if (== () (collapse (once (match {s} {a} {a})))) (add-atom {s} {a}) (empty))",
            "(if (== (collapse (match {s} {a} {a})) ()) (add-atom {s} {a}) (empty))",
            "(if (== (size-atom (collapse (match {s} {a} t))) 0) (add-atom {s} {a}) (empty))",
            "(if (!= (collapse (once (match {s} {a} {a}))) ()) (empty) (add-atom {s} {a}))",
            "(if (!= () (collapse (match {s} {a} {a}))) (empty) (add-atom {s} {a}))",
            "(if (== 0 (size-atom (collapse (once (match {s} {a} t))))) (add-atom {s} {a}) (empty))",
            "(if (!= (size-atom (collapse (match {s} {a} t))) 0) present (add-atom {s} {a}))",
            "(if (== (size-atom (collapse (match {s} {a} t))) 1) (add-atom {s} (again {a})) (add-atom {s} {a}))",
            "(case (once (match {s} {a} {a})) ((Empty (add-atom {s} {a})) ($_ (empty))))",
            "(case (once (match {s} {a} t)) (($w present) (Empty (add-atom {s} {a}))))",
        ]
        for index in range(exists.randrange(1, 4)):
            target = exists.choice(("&s", "&b"))
            body = exists.choice(spellings).format(s="$S", a="$A")
            lines.append(f"(= (nodup{index} $S $A) {body})")
            lines.append(f"!(size-atom (collapse (match &self (edge $x $y) (nodup{index} {target} (seen{index} $x)))))")
            lines.append(f"!(let $h (collapse (match {target} (seen{index} $v) $v)) (sort-atom $h))")
            top = exists.choice(spellings).format(s=target, a=f"(seen{index} {exists.randrange(nodes)})")
            lines.append(f"!(let $h (collapse {top}) (size-atom $h))")
        lines.append("!(let $h (collapse (if (!= (collapse (match &self (edge $p $q) $q)) ()) yes no)) $h)")
        lines.append(f"!(if (!= (collapse (once (match &self (edge {exists.randrange(nodes)} $q) t))) ()) present absent)")
    # Rows holding variables added into a set and a bag, from an equation and
    # at top level: the template's own variables, one repeated, a caller's
    # shared by two rows, one bound after the addition, a payload holding a
    # call the addition never evaluates and one with a variable head, a list
    # with an open tail, a row of 65 variables; then patterns that read
    # where the variables stand.
    admit = random.Random(zlib.crc32(("admit\n" + "\n".join(lines)).encode()))
    if admit.random() < 0.6:
        many = " ".join(f"$m{i}" for i in range(65))
        lines += ["(= (ag $v) (+ $v 1))", "(fnrow ag)", "(fnrow 7)"]
        # rows holding variables inside terms, which the native path
        # declines: a second pattern binds a variable the first row brought
        # in, and the addition must carry its value
        lines += ["(vrow (p $q) 1)", "(vrow (p 4) 2)", "(vrow $q $q)",
                  "(vpair (p 5) 7)", "(vpair (p 4) 8)", "(vpair $r 9)"]
        bodies = [
            "(match &self (vrow $vx $vy) (match &self (vpair $vx $vz) (add-atom $S (oh $vx $vz $f $x))))",
            "(match &self (vrow $vx $vy) (match &self (vpair $vx $vz) (add-atom $S (oi $vx $vy $vz))))",
            "(match &self (vrow $vx $vy) (let $u (add-atom $S (oj $vx $c)) (match &self (vpair $vx $vz) ok)))",
            "(add-atom $S (oa $x $f))",
            "(add-atom $S (oa $f $f))",
            "(add-atom $S (oa (p $f) $y $g))",
            "(add-atom $S (oa $x (p $f $y)))",
            "(let* (($a (add-atom $S (ob $x $c))) ($b (add-atom $S (oc $c $y)))) ok)",
            "(add-atom $S (od (ag $x) $f))",
            "(match &self (fnrow $h) (add-atom $S (od ($h $x) $f)))",
            "(let $l (cons-atom $x $f) (add-atom $S (oe $l)))",
            f"(add-atom $S (of $x {many}))",
            "(let $u (add-atom $S (og $x $f)) (let $f $y ok))",
        ]
        for index in range(admit.randrange(2, 6)):
            body = admit.choice(bodies)
            lines.append(f"(= (adm{index} $S $c) (match &self (edge $x $y) {body}))")
            for target in admit.sample(("&s", "&b"), admit.randrange(1, 3)):
                caller = admit.choice(("$z", "5", "(p $z)"))
                lines.append(f"!(size-atom (collapse (adm{index} {target} {caller})))")
            if admit.random() < 0.5:
                top = admit.choice(bodies).replace("$S", admit.choice(("&s", "&b")))
                lines.append(f"!(size-atom (collapse (match &self (edge $x $y) {top})))")
        if admit.random() < 0.4:
            lines.append(f"!(add-atom &s (oa $w {admit.randrange(nodes)}))")
        for space in ("&s", "&b"):
            lines += [
                f"!(let $h (collapse (match {space} (oa $u $u) $u)) (sort-atom $h))",
                f"!(let $h (collapse (match {space} (oa 1 99) t)) (size-atom $h))",
                f"!(let $h (collapse (match {space} (oa (p 98) 97 $w) t)) (size-atom $h))",
                f"!(let $h (collapse (match {space} (ob $u $w) (match {space} (oc $w $k) t))) (size-atom $h))",
                f"!(let $h (collapse (match {space} (od $u 99) $u)) (sort-atom $h))",
                f"!(let $h (collapse (match {space} (og $u 99) $u)) (sort-atom $h))",
                f"!(let $h (collapse (match {space} (oe $u) $u)) (size-atom $h))",
                f"!(let $h (collapse (match {space} (oh (p 5) $u $w $k) $u)) (sort-atom $h))",
                f"!(let $h (collapse (match {space} (oi (p 6) $u $w) $w)) (sort-atom $h))",
                f"!(let $h (collapse (match {space} (oj (p 6) $u) t)) (size-atom $h))",
            ]
    # A let destructuring a matched variable inside an equation over rows
    # holding variables, which the native path declines and the equation
    # compiler runs: the variable read again in the body, passed in by the
    # caller ground, open or apart, or read nowhere else.
    destruct = random.Random(zlib.crc32(("destruct\n" + "\n".join(lines)).encode()))
    if destruct.random() < 0.5:
        lines += ["(dpair (p 1) 2)", "(dpair (p $q) 3)", "(dpair (r 4) 5)",
                  "(dpair $q 6)"]
        shapes = [
            ("(ds{i})", "(match &self (dpair $x $y) (let (p $a) $x (dout $a $x)))",
             ["(ds{i})"]),
            ("(ds{i} $x)", "(match &self (dpair $x $y) (let (p $a) $x (dout $a $y)))",
             ["(ds{i} (p 1))", "(ds{i} $z)", "(ds{i} (r 4))"]),
            ("(ds{i})", "(match &self (dpair $x $y) (let (p $a) $x (dout $a)))",
             ["(ds{i})"]),
            ("(ds{i})", "(match &self (dpair $x $y) (let (p $a) $x (let $u (add-atom &s (dseen $x)) (dout $a))))",
             ["(ds{i})"]),
        ]
        for i in range(destruct.randrange(1, 4)):
            head, body, calls = destruct.choice(shapes)
            lines.append(f"(= {head.format(i=i)} {body})")
            for call in calls:
                lines.append(f"!(let $h (collapse {call.format(i=i)}) (sort-atom $h))")
    # Templates headed by a matched variable, whose value is data in some
    # rows and in others a function, one of no answer, a grounded
    # operation, a form or an expression: collected alone, after a join,
    # counted, and collected then spread into effects.
    varhead = random.Random(zlib.crc32(("varhead\n" + "\n".join(lines)).encode()))
    if varhead.random() < 0.5:
        heads = ["vn1", "vn2", "7", "vf", "vg", "+", "if", "(p 1)"]
        chosen = varhead.sample(heads, varhead.randrange(1, 6))
        lines += ["(= (vf $x) (* $x 10))", "(= (vg $x) (empty))"]
        lines += [f"(vrowh {h} {i % nodes})" for i, h in enumerate(chosen)]
        lines.append("!(let $r (collapse (match &self (vrowh $h $v) ($h $v))) (sort-atom $r))")
        lines.append("!(let $r (collapse (match &self (, (vrowh $h $v) (edge $v $w)) ($h $w))) (sort-atom $r))")
        lines.append("!(let $ps (collapse (match &self (vrowh $h $v) ($h $v))) (size-atom $ps))")
        lines.append("!(let $ps (collapse (match &self (vrowh $h $v) ($h $v))) (collapse (let ($h $v) (superpose $ps) (add-atom &s (vseen $h $v)))))")
        lines.append("!(let $r (collapse (match &s (vseen $h $v) ($h $v))) (sort-atom $r))")
    # A collection's distinct elements spread: unique-atom over a collapse
    # of a match, each element added into a bag or answered, counted or
    # collected; templates that drop variables, so that solutions repeat
    # an element, repeat one, or are a constant, and a spread taking an
    # element apart.  Controls: a spread reading a variable of the match,
    # which its scope does not bind, and, rarely, an equation extending
    # unique-atom, whose answers PeTTa's extended profile adds.
    distinct = random.Random(zlib.crc32(("distinct\n" + "\n".join(lines)).encode()))
    if distinct.random() < 0.5:
        lines.append("!(bind! &u (new-space))")
        if distinct.random() < 0.1:
            lines.append("(= (unique-atom $l) $l)")
        joins = (("(edge $x $y)", ("$x", "$y")),
                 ("(, (edge $x $y) (edge $y $z))", ("$x", "$y", "$z")),
                 ("(, (edge $x $y) (lab $x $s $y))", ("$x", "$y", "$s")))
        for index in range(distinct.randrange(1, 4)):
            join, bound = distinct.choice(joins)
            shape = distinct.random()
            if shape < 0.25:
                template = f"(pr {distinct.choice(bound)})"
            elif shape < 0.5:
                template = f"(pr {' '.join(distinct.sample(bound, 2))})"
            elif shape < 0.65:
                v = distinct.choice(bound)
                template = f"(pr {v} {v})"
            elif shape < 0.75:
                template = "(pr k)"
            else:
                template = distinct.choice(bound)
            pat, spread = "$a", distinct.choice(("(add-atom &u $a)", "(seen $a)",
                                                 "(add-atom &u (w $a))"))
            if template.startswith("(pr ") and distinct.random() < 0.2:
                parts = template[4:-1].split()
                pat = "(pr " + " ".join(f"$p{i}" for i in range(len(parts))) + ")"
                spread = "(add-atom &u (got " + " ".join(f"$p{i}" for i in range(len(parts))) + "))"
            if distinct.random() < 0.1:
                # reads the match's variable outside the match's scope
                spread = f"(pair $a {bound[0]})"
                pat = "$a"
            elements = f"(unique-atom (collapse (match &self {join} {template})))"
            spread = f"(let {pat} (superpose $ps) {spread})"
            if distinct.random() < 0.6:
                lines.append(f"!(let $ps {elements} (size-atom (collapse {spread})))")
            else:
                lines.append(f"!(let $ps {elements} (collapse {spread}))")
        lines.append("!(let $h (collapse (get-atoms &u)) (sort-atom $h))")
    # Equations a round adds: a function the template calls gains an
    # equation keyed by each row as the round runs, and PeTTa calls it in
    # each later row with the equations the earlier rows added; the native
    # path, which reads the program once, declines a round adding one.
    # Controls: an argument of two answers, which PeTTa evaluates once
    # before the call, to a function whose body reads its parameter twice or
    # not at all; and a call inside a join whose variables the body's own
    # spell alike, which an activation renames apart.
    live = random.Random(zlib.crc32(("live\n" + "\n".join(lines)).encode()))
    if live.random() < 0.5:
        lines += ["(= (lf $x) (lbase $x))", "(= (ltwo) (superpose (0 1)))",
                  "(= (ldup $x) (lpair $x $x))", "(= (ldrop $x) ldropped)",
                  "(= (lreach $k) (match &self (edge $k $b) (lhop $k $b)))"]
        for call in live.sample(("(ldup (ltwo))", "(ldrop (ltwo))", "(lreach $a)",
                                 "(lreach $b)"), live.randrange(1, 5)):
            lines.append(f"!(let $h (collapse (match &self (edge $a $b) {call})) (sort-atom $h))")
        for index in range(live.randrange(1, 4)):
            effect = live.choice(("(add-atom &self (= (lf $b) (lextra $b)))",
                                  "(add-atom &self (= (lf $a) (lagain $a)))",
                                  "(add-atom &self (= (lg $b) $a))"))
            call = live.choice(("(lf $a)", "(lf $b)", "(lg $a)", "(lf (ltwo))"))
            lines.append(f"!(let $h (collapse (match &self (edge $a $b) "
                         f"(let $u {effect} {call}))) (sort-atom $h))")
        lines.append("!(let $h (collapse (match &self (edge $a $b) (lf (ltwo)))) (sort-atom $h))")
        lines.append("!(let $h (collapse (lf 1)) (sort-atom $h))")
        lines.append("!(let $h (collapse (lg 1)) (sort-atom $h))")
    for space in ("&s", "&b"):
        lines.append(f"!(let $h (collapse (get-atoms {space})) (sort-atom $h))")
    lines.append("!(let $h (collapse (match &self (edge $u $v) (edge $u $v))) (sort-atom $h))")
    lines.append("!(let $h (collapse (match &self (r $u $v) (r $u $v))) (sort-atom $h))")
    lines.append("!(let $h (collapse (match &self $any $any)) (size-atom $h))")
    return "\n".join(lines) + "\n"


def elements(text):
    """The top-level elements of a printed collection's inside."""
    items, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        elif c == ' ' and depth == 0:
            if i > start:
                items.append(text[start:i])
            start = i + 1
    if start < len(text):
        items.append(text[start:])
    return items


def canonical(output):
    lines = []
    segment = None
    for line in re.sub(r"\$[A-Za-z0-9_#]+", "$_", output).splitlines():
        # a directive's answers, between markers, are a bag of lines
        if line == "seg-begin":
            segment = []
        elif line == "seg-end" and segment is not None:
            lines.extend(sorted(segment))
            segment = None
        elif segment is not None:
            segment.append(line)
        else:
            if line.startswith("(") and line.endswith(")"):
                line = "(" + " ".join(sorted(elements(line[1:-1]))) + ")"
            lines.append(line)
    return "\n".join(lines)


def run(binary, path, reference, stats):
    env = dict(os.environ)
    env.pop("CETTA_NATIVE_QUERY_REFERENCE", None)
    if reference:
        env["CETTA_NATIVE_QUERY_REFERENCE"] = "1"
        env["CETTA_FOLD_ARITH_REFERENCE"] = "1"
    command = [binary, "--profile", "extended", "--lang", "petta"]
    if stats:
        command.append("--emit-runtime-stats")
    p = subprocess.run(command + [path], cwd=ROOT, env=env,
                       capture_output=True, text=True, timeout=120)
    admissions = dense = 0
    for line in p.stderr.splitlines():
        fields = line.split()
        if fields[:2] == ["runtime-counter", "native-query-admission"]:
            admissions = int(fields[2])
        if fields[:2] in (["runtime-counter", "dense-relation-admission"],
                          ["runtime-counter", "native-query-bits-leg"]):
            dense += int(fields[2])
    return (p.returncode, canonical(p.stdout)), admissions, dense


# Programs the ordinary path ends with an error, which the native path must
# end alike: a function of one equation called, inside a collection, with
# another arity than its equation's, which PeTTa refuses.
FIXED = [
    "(edge 1 2)\n(edge 2 3)\n(= (lf $x) (lbase $x))\n"
    "!(let $h (collapse (match &self (edge $a $b) (lf $a $b))) (sort-atom $h))\n",
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--programs", type=int, default=300)
    parser.add_argument("--seed", type=int, default=20261027)
    parser.add_argument("--stats", action="store_true")
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    rng = random.Random(args.seed)
    admitted = dense_programs = 0
    with tempfile.TemporaryDirectory(prefix="cetta-native-query-") as work:
        for number, source in enumerate(FIXED):
            path = os.path.join(work, f"fixed_{number}.metta")
            with open(path, "w") as f:
                f.write(source)
            native, _, _ = run(binary, path, False, args.stats)
            reference, _, _ = run(binary, path, True, args.stats)
            if native != reference:
                print(f"FAIL: fixed program {number} ends differently on the native path")
                print(source)
                print("native:", native)
                print("reference:", reference)
                return 1
        for number in range(args.programs):
            source = program(rng)
            path = os.path.join(work, f"query_{number}.metta")
            with open(path, "w") as f:
                f.write(source)
            native, admissions, dense = run(binary, path, False, args.stats)
            reference, declined, _ = run(binary, path, True, args.stats)
            if reference[0] != 0:
                print(f"FAIL: program {number} does not run on the ordinary path")
                print(source)
                print("reference:", reference)
                return 1
            if native != reference or declined:
                print(f"FAIL: program {number} answers differently on the native path")
                print(source)
                print("native:", native)
                print("reference:", reference)
                return 1
            admitted += admissions
            dense_programs += dense > 0
    if args.stats and admitted < args.programs:
        print(f"FAIL: the native path answered {admitted} collections in "
              f"{args.programs} programs")
        return 1
    if args.stats and dense_programs * 5 < args.programs:
        print(f"FAIL: rows of bits joined in {dense_programs} of "
              f"{args.programs} programs")
        return 1
    print(f"PASS: {args.programs} programs answer and leave the same spaces "
          f"on the native query path as on the ordinary one"
          + (f"; it answered {admitted} collections, by rows of bits in "
             f"{dense_programs} programs" if args.stats else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
