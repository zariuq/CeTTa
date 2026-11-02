#!/usr/bin/env python3
"""Small, replayable source schemas for observer-directed native qualification.

The scalar grammar mirrors TypedScalar's data/test distinction. Python's
constructor checks are not a Lean proof; --emit-lean in the driver exports
the scalar admission controls against the checked source extractor.
Other families use the runtime's existing fixture forms and an explicit
observation contract, never a guess from the printed value's shape.
"""
from dataclasses import dataclass, replace
import json
import random
import re
from compare_answers import TOKEN

FAMILIES = (
    "scalar-fold", "filtered-match", "grouped-count", "listed-match",
    "dead-rule", "kept-relation", "row-takeover", "open-join",
    "guard-batch", "arithmetic-fold", "saturation", "relaxation", "match-existence",
)
OBSERVERS = ("value", "tuple", "bag", "catch", "effect", "ordered")
MARKER = "__cetta_observer_probe_"
LITERAL = re.compile(r"(?:-?[0-9]+(?:\.[0-9]+)?|[a-z][a-z0-9_-]*)\Z")


def integer_rank(value):
    number = int(value)
    return 0 if number == 0 else 1 if number == 1 else 2 if number == -1 else 3 + 2 * abs(number) + (number < 0)

# These identify exact runtime counters only where the inspected source has
# them. Generic query admissions are recorded separately, not attributed to
# a particular specialization merely because that specialization was asked for.
ROUTE_COUNTERS = {
    "arithmetic-fold": ("native-fold-ordered-scan", "native-fold-run",
                        "native-fold-moments", "native-fold-bigint"),
}
SOURCE_ANCHORS = {
    "scalar-fold": ("src/eval.c", "petta_eval_scalar_fold"),
    "filtered-match": ("src/eval.c", "petta_eval_machine_native_filter"),
    "grouped-count": ("src/eval.c", "petta_grouped_count_run"),
    "listed-match": ("src/eval.c", "petta_listed_match_run"),
    "dead-rule": ("src/eval.c", "petta_saturation_region_run"),
    "kept-relation": ("src/space.c", "space_dense_take_publishing"),
    "row-takeover": ("src/space.c", "space_dense_take_over_rows"),
    "open-join": ("src/native_open_query.c", "cetta_native_open_query_run"),
    "guard-batch": ("src/native_query.c", "cetta_native_query_run"),
    "arithmetic-fold": ("src/native_fold.c", "cetta_native_foldall"),
    "saturation": ("src/eval.c", "petta_saturation_region_run"),
    "relaxation": ("src/eval.c", "petta_relaxation_region_run"),
    "match-existence": ("src/space.c", "space_match_exists_ground_exact"),
}


@dataclass(frozen=True)
class Expr:
    op: str
    kind: str
    args: tuple = ()
    token: str = ""

    def __post_init__(self):
        if self.kind not in ("data", "test"):
            raise ValueError("unknown scalar result kind")
        if self.op in ("integer", "symbol", "read"):
            if self.kind != "data" or self.args:
                raise ValueError("data leaf has an invalid kind or children")
            if self.op == "integer" and not re.fullmatch(r"-?[0-9]+", self.token):
                raise ValueError("invalid integer literal")
            if self.op == "symbol" and not re.fullmatch(r"[a-z][a-z0-9_-]*", self.token):
                raise ValueError("invalid inert symbol spelling")
            if self.op == "read" and self.token not in ("$a", "$x"):
                raise ValueError("unknown scalar capture")
        elif self.op == "boolean":
            if self.kind != "test" or self.token not in ("true", "false") or self.args:
                raise ValueError("invalid Boolean leaf")
        elif self.op in ("+", "-", "<", "<=", ">", ">="):
            expected = "data" if self.op in ("+", "-") else "test"
            if self.kind != expected or len(self.args) != 2 or any(x.kind != "data" for x in self.args):
                raise ValueError("arithmetic/order operands must be data")
        elif self.op == "==":
            if self.kind != "test" or len(self.args) != 2:
                raise ValueError("equality returns a test")
        elif self.op == "if":
            if len(self.args) != 3 or self.args[0].kind != "test" or any(x.kind != self.kind for x in self.args[1:]):
                raise ValueError("conditional result kinds disagree")
        elif self.op == "bad-add":
            if self.kind != "data" or len(self.args) != 2 or self.args[0].kind != "test" or self.args[1].kind != "data":
                raise ValueError("invalid negative-control schema")
        else:
            raise ValueError(f"unknown expression operation {self.op}")

    def render(self):
        if self.op in ("integer", "symbol", "boolean", "read"):
            return self.token
        head = "+" if self.op == "bad-add" else self.op
        return "(" + " ".join([head] + [arg.render() for arg in self.args]) + ")"

    def to_dict(self):
        return {"op": self.op, "kind": self.kind, "token": self.token,
                "args": [x.to_dict() for x in self.args]}

    @staticmethod
    def from_dict(value):
        return Expr(value["op"], value["kind"], tuple(Expr.from_dict(x) for x in value["args"]), value["token"])

    def reductions(self):
        for arg in self.args:
            if arg.kind == self.kind:
                yield arg
        if self.op == "integer":
            for value in ("0", "1", "-1"):
                if integer_rank(value) < integer_rank(self.token):
                    yield Expr("integer", "data", token=value)
        for i, arg in enumerate(self.args):
            for smaller in arg.reductions():
                try:
                    yield replace(self, args=self.args[:i] + (smaller,) + self.args[i + 1:])
                except ValueError:
                    continue

    def lean(self):
        if self.op == "integer":
            return f"(.grounded (.int ({self.token})))"
        if self.op == "boolean":
            return f"(.grounded (.bool {self.token}))"
        if self.op == "symbol":
            return f"(.symbol {json.dumps(self.token)})"
        if self.op == "read":
            return f"(.var {json.dumps(self.token[1:])})"
        head = "+" if self.op == "bad-add" else self.op
        return "(.expression [.symbol " + json.dumps(head) + ", " + ", ".join(x.lean() for x in self.args) + "])"


def integer(value):
    return Expr("integer", "data", token=str(value))


def read(name):
    return Expr("read", "data", token="$" + name)


def expression(rng, depth, kind):
    if depth == 0:
        if kind == "test":
            return Expr("==", "test", (read("x"), integer(rng.choice((0, 1, 2)))))
        return rng.choice((read("a"), read("x"), integer(rng.randint(-3, 3))))
    if kind == "test":
        if rng.randrange(3) == 0:
            return Expr("==", "test", (expression(rng, depth - 1, "test"), integer(rng.randrange(2))))
        return Expr(rng.choice(("==", "<", "<=", ">", ">=")), "test",
                    (expression(rng, depth - 1, "data"), expression(rng, depth - 1, "data")))
    if rng.randrange(3) == 0:
        return Expr("if", "data", (expression(rng, depth - 1, "test"),
                    expression(rng, depth - 1, "data"), expression(rng, depth - 1, "data")))
    return Expr(rng.choice(("+", "-")), "data",
                (expression(rng, depth - 1, "data"), expression(rng, depth - 1, "data")))


@dataclass(frozen=True)
class Case:
    number: int
    family: str
    observer: str
    values: tuple
    expr: Expr
    nested: bool = False
    variant: str = "base"
    negative: bool = False
    history: int = 0
    consumer: str = "stream"
    tagged: bool = False
    effectful: bool = False
    boundary: str = "ordinary"
    producer: str = "scan"
    fact_shape: str = "deep"

    def __post_init__(self):
        if self.family not in FAMILIES or self.observer not in OBSERVERS:
            raise ValueError("unknown case family or observer")
        if not self.values or any(not LITERAL.fullmatch(value) for value in self.values):
            raise ValueError("case inputs must be nonempty scalar literal sequences")
        if self.family == "relaxation" and any(not re.fullmatch(r"-?[0-9]+", value) for value in self.values):
            raise ValueError("relaxation schema needs integer costs")
        if self.variant not in ("base", "irrelevant-definition"):
            raise ValueError("unknown metamorphic variant")
        if self.consumer not in ("stream", "count", "exists"):
            raise ValueError("unknown region consumer")
        if any(not isinstance(flag, bool) for flag in (self.nested, self.negative, self.tagged, self.effectful)):
            raise ValueError("case flags must be Boolean")
        boundaries = {"empty-group": ("grouped-count",), "preheld-guard": ("guard-batch",),
                      "ground-first": ("open-join",), "incomplete-coverage": ("dead-rule", "saturation"),
                      "open-existence": ("match-existence",)}
        if self.boundary != "ordinary" and self.family not in boundaries.get(self.boundary, ()):
            raise ValueError("boundary does not belong to this region family")
        if self.boundary == "incomplete-coverage" and not self.negative:
            raise ValueError("incomplete coverage is a refusal control")
        if self.producer not in ("scan", "run", "moments") or (self.family != "arithmetic-fold" and self.producer != "scan"):
            raise ValueError("producer does not belong to this region family")
        if self.fact_shape not in ("deep", "flat", "flat-star") or (self.fact_shape != "deep" and self.family not in
                ("kept-relation", "guard-batch", "row-takeover")):
            raise ValueError("fact shape does not belong to this region family")
        if not isinstance(self.history, int) or not 0 <= self.history <= 100000:
            raise ValueError("invalid history length")
        if self.family == "filtered-match" and self.expr.kind != "test":
            raise ValueError("filtered schema requires a predicate")
        if self.family == "arithmetic-fold" and not (self.expr.op == "+" and self.expr.args[0] == read("a") and "$a" not in self.expr.args[1].render()):
            raise ValueError("unordered arithmetic fold requires the additive schema")

    def to_dict(self):
        return {"number": self.number, "family": self.family, "observer": self.observer,
                "values": list(self.values), "expr": self.expr.to_dict(), "nested": self.nested,
                "variant": self.variant, "negative": self.negative, "history": self.history,
                "consumer": self.consumer, "tagged": self.tagged, "effectful": self.effectful,
                "boundary": self.boundary, "producer": self.producer, "fact_shape": self.fact_shape}

    @staticmethod
    def from_dict(value):
        return Case(**{**value, "values": tuple(value["values"]), "expr": Expr.from_dict(value["expr"])})

    def key(self):
        return json.dumps(self.to_dict(), sort_keys=True)

    def reductions(self):
        # This is a declared local reduction system, not global minimization.
        for width in sorted({1, max(1, len(self.values) // 2)}, reverse=True):
            for start in range(0, len(self.values), width):
                reduced = self.values[:start] + self.values[start + width:]
                if reduced:
                    yield replace(self, values=reduced)
        if self.family in ("scalar-fold", "filtered-match", "arithmetic-fold"):
            for smaller in self.expr.reductions():
                try:
                    yield replace(self, expr=smaller)
                except ValueError:
                    continue
        for i, value in enumerate(self.values):
            if re.fullmatch(r"-?[0-9]+", value):
                for smaller in ("0", "1", "-1"):
                    if integer_rank(smaller) < integer_rank(value):
                        yield replace(self, values=self.values[:i] + (smaller,) + self.values[i + 1:])
        if self.nested:
            yield replace(self, nested=False)
        if self.boundary != "ordinary":
            yield replace(self, boundary="ordinary")
        for count in sorted({0, 1, self.history // 2}):
            if count < self.history:
                yield replace(self, history=count)

    def render(self):
        setup, region, after = build_region(self)
        if self.consumer != "stream":
            region = f"(size-atom (collapse {region}))"
            if self.consumer == "exists":
                region = f"(not (== {region} 0))"
        contracts = {}
        queries = []
        occupied = set(TOKEN.findall("\n".join(setup + [region] + after)))

        def fresh(stem):
            name = "$__cetta_observer_" + stem
            while name in occupied:
                name += "_"
            occupied.add(name)
            return name

        result_var, value_var, key_var, keyed_var = fresh("result"), fresh("value"), fresh("key"), fresh("keyed_value")
        collection_var, print_var, store_var = fresh("collection"), fresh("printed"), fresh("stored")
        # Generated region results are ground. A printed structural key
        # distinguishes e.g. integer 1 and float 1.0 before their payloads
        # are compared, avoiding an unspecified stable-sort tie order.
        keyed = f"(sort-atom (collapse (let {keyed_var} {region} (sort-key (repr {keyed_var}) {keyed_var}))))"

        def observe(code, mode="answers"):
            marker = MARKER + str(len(queries))
            contracts[marker] = mode
            queries.append(f"!(let {result_var} {code} ({marker} {result_var}))")

        if self.observer == "value":
            observe(region)
        elif self.observer == "tuple":
            observe(f"(let {value_var} {region} (field {value_var} (ordered 1 0)))")
        elif self.observer == "bag":
            observe(f"(collapse {region})", "bag")
        elif self.observer == "catch":
            observe(f"(collapse (catch {region}))", "bag")
        elif self.observer == "effect":
            setup += ["!(bind! &observed-effects (new-space))"]
            # superpose receives a ready list; its raw argument must not be
            # the unevaluated syntax of the sorting computation.
            ordered = f"(superpose {collection_var})"
            effects = f"(let {print_var} (println! (observed-effect {value_var})) (let {store_var} (add-atom &observed-effects (seen {value_var})) {value_var}))"
            observe(f"(let {collection_var} {keyed} (collapse (let (sort-key {key_var} {value_var}) {ordered} {effects})))", "bag")
            after += ["(collapse (match &observed-effects (seen $v) $v))"]
        elif self.observer == "ordered":
            # The source explicitly orders by a ground structural key. The
            # comparator must not quotient this list back to an answer bag.
            observe(keyed, "ordered")
        else:
            raise ValueError("unknown observer")
        for query in after:
            observe(query, "bag")
        if self.variant == "irrelevant-definition":
            setup += ["(= (unrelated-observer-fixture $z) $z)"]
        return "\n".join(setup + queries) + "\n", contracts


def build_region(case):
    values = case.values
    facts = [f"(row {v})" if not case.nested else f"(row (box {v}))" for v in values]
    pattern = "(row $x)" if not case.nested else "(row (box $x))"
    code = case.expr.render()
    family = case.family
    if family == "match-existence":
        wrap = lambda value: f"(box {value})" if case.nested else value
        setup = ["!(bind! &exists-input (new-space set))"]
        inputs = ("$stored",) if case.boundary == "open-existence" else values
        setup += [f"!(add-atom &exists-input (exists-fact {wrap(value)}))" for value in inputs]
        key = values[0]
        if case.negative:
            key = "missing-existence-key"
            while key in values:
                key += "-fresh"
        return setup, f"(== (collapse (match &exists-input (exists-fact {wrap(key)}) t)) ())", []
    if family == "scalar-fold":
        return [], f"(foldl-atom ({' '.join(values)}) 0 $a $x {code})", []
    if family == "arithmetic-fold":
        # Its input match is unordered. The generated positive step is
        # additive in the accumulator, so cross-storage comparison is valid.
        template = "2" if case.producer == "run" else "$x"
        if case.producer == "moments":
            # Two independent choice occurrences multiply every input row's
            # contribution. The step is additive, so only the bag matters.
            facts += ["(fold-choice left)", "(fold-choice right)"]
            pattern = f"(, {pattern} (fold-choice $choice))"
        return facts + [f"(= (observer-arithmetic-step $x $a) {code})"], f"(foldall observer-arithmetic-step (match &self {pattern} {template}) 0)", []
    if family == "filtered-match":
        test = code.replace("$a", "0")
        return facts, f"(match &self {pattern} (if {test} (hit $x) (empty)))", []
    if family == "open-join":
        if case.effectful:
            links = ["!(add-atom &open-input (link $shared (pair $shared $shared)))",
                     "!(add-atom &open-input (link 1 (pair 1 2)))"]
            if case.boundary == "ground-first":
                links.reverse()
            setup = facts + ["!(bind! &open-input (new-space set))", "!(bind! &open-output (new-space set))"] + links
            return setup, f"(match &self {pattern} (match &open-input (link $x $y) (add-atom &open-output (open-hit $x $y))))", ["(collapse (match &open-output (open-hit $x $y) (open-hit $x $y)))"]
        setup = facts + ["(link $shared (pair $shared $shared))", "(link 1 (pair 1 2))"]
        answer = "(open-hit $x $y)" if case.tagged else "($x $y)"
        return setup, f"(match &self (, {pattern} (link $x $y)) {answer})", []
    if family == "grouped-count":
        setup = facts + [f"(bucket {v})" for v in values[::2]] + ["!(bind! &grouped-output (new-space))"]
        if case.boundary == "empty-group":
            missing = "missing-observer-key"
            while missing in values:
                missing += "-fresh"
            setup += [f"(bucket {missing})"]
        region = f"(match &self (bucket $k) (let $n (size-atom (collapse (match &self {pattern.replace('$x', '$k')} t))) (let $_group_insert (add-atom &grouped-output (count $k $n)) (count $k $n))))"
        return setup, region, ["(collapse (match &grouped-output (count $k $n) ($k $n)))"]
    if family == "listed-match":
        setup = [f"(edge {v} node{i % 3})" if case.effectful else f"(edge {v} (payload {i % 3}))"
                 for i, v in enumerate(values)]
        listing = " ".join(values)
        if case.effectful:
            setup += ["!(bind! &listed-output (new-space))"]
            return setup, f"(let $u (superpose ({listing})) (match &self (edge $u $v) (add-atom &listed-output (listed-hit $u $v))))", ["(collapse (match &listed-output (listed-hit $u $v) (listed-hit $u $v)))"]
        answer = "(listed-hit $u $v)" if case.tagged else "($u $v)"
        return setup, f"(let $u (superpose ({listing})) (match &self (edge $u $v) {answer}))", []
    if family in ("kept-relation", "guard-batch", "row-takeover"):
        def kept(value):
            if case.fact_shape == "flat-star":
                return f"(kept shared-key {value})"
            part = f"(box {value})" if case.fact_shape == "deep" else value
            return f"(kept {value} {part})"
        setup = facts + ["!(bind! &kept (new-space))", "!(bind! &seen (new-space set))",
                         f"!(size-atom (collapse (match &self {pattern} (add-atom &kept {kept('$x')}))))"]
        if case.boundary == "preheld-guard":
            setup += [f"!(add-atom &seen (seen {values[0]}))"]
        if family == "row-takeover":
            # Promotion/read/removal history; actual take-over coverage must
            # come from a frozen implementation's route evidence.
            setup += [f"!(add-atom &kept {kept(values[0])})"]
            if case.history:
                setup += [f"(= (fill-observer-history $i $n) (if (>= $i $n) done (let $_ (add-atom &kept {kept('$i')}) (fill-observer-history (+ $i 1) $n))))",
                          f"!(fill-observer-history 0 {case.history})"]
            region = "(match &kept (kept $x $part) (let $_ (remove-atom &kept (kept $x $part)) ($x $part)))"
        else:
            test = "(== (collapse (match &seen (seen $x) t)) ())"
            region = f"(match &kept (kept $x $part) (if {test} (let $_ (add-atom &seen (seen $x)) (accepted $x)) (empty)))"
        return setup, region, ["(collapse (match &kept (kept $x $part) ($x $part)))",
                               "(collapse (match &seen (seen $x) $x))"]
    if family in ("dead-rule", "saturation"):
        if case.boundary == "incomplete-coverage":
            setup = ["(gate g)", "(seed p 1)", "(seed q 1)", "!(bind! &closure (new-space set))",
                     "(= (insert-node $k $x) (if (== (collapse (match &closure (node $k $x) t)) ()) (let $_ (add-atom &closure (node $k $x)) (fire-node $k $x)) (empty)))",
                     "(= (fire-node p $x) (match &self (gate g) (match &closure (node q $x) (insert-node r $x))))",
                     "(= (fire-node q $x) (match &self (gate g) (match &closure (node p $x) (insert-node s $x))))"]
            # Either sequential seed order makes one consequence (three facts
            # total). Unsound batched admission makes both (four). The source
            # deliberately observes the order-independent count, not which
            # of the two consequences the ordinary seed order selected.
            return setup, "(match &self (seed $k $x) (insert-node $k $x))", ["(collapse (size-atom (collapse (match &closure (node $k $x) t))))"]
        setup = ["!(bind! &closure (new-space set))", f"(seed {values[0]})"]
        setup += [f"(arc {values[i]} {values[i + 1]})" for i in range(len(values) - 1)]
        setup += ["(= (insert-node $x) (if (== (collapse (match &closure (node $x) t)) ()) (let $_ (add-atom &closure (node $x)) (fire-node $x)) (empty)))",
                  "(= (fire-node $x) (match &self (arc $x $y) (insert-node $y)))"]
        if family == "dead-rule":
            setup += ["(= (fire-node $x) (match &closure (never $x $y) (match &closure (reverse $y $x) (insert-node $y))))"]
        return setup, "(match &self (seed $initial) (insert-node $initial))", ["(collapse (match &closure (node $v) $v))"]
    if family == "relaxation":
        setup = ["!(bind! &distances (new-space set))", "!(add-atom &distances (distance start 0))"]
        setup += [f"(edge start n{i} {abs(int(v))})" for i, v in enumerate(values)]
        setup += ["(= (improve $v $n) (let $old (collapse (match &distances (distance $v $d) $d)) (if (== $old ()) (let $_ (add-atom &distances (distance $v $n)) $v) (let ($d) $old (if (< $n $d) (let* (($_ (remove-atom &distances (distance $v $d))) ($_ (add-atom &distances (distance $v $n)))) $v) (empty))))))",
                  "(= (relax $u) (match &distances (distance $u $d) (match &self (edge $u $v $c) (improve $v (+ $d $c)))))"]
        return setup, "(let $frontier (superpose (start)) (relax $frontier))", ["(collapse (match &distances (distance $v $d) ($v $d)))"]
    raise ValueError(f"unimplemented family {family}")


def generate(seed, per_family, families=FAMILIES):
    rng = random.Random(seed)
    cases = []
    for family in families:
        if family == "match-existence":
            continue  # Append this extension without renumbering older cases.
        for index in range(per_family):
            observer = OBSERVERS[index % len(OBSERVERS)]
            values = tuple(str(rng.randint(0, 3)) for _ in range(1 + index % 7))
            if family in ("listed-match", "row-takeover") and index % 3 == 0:
                values *= 20
            mixed = family in ("filtered-match", "grouped-count", "listed-match", "kept-relation", "guard-batch", "row-takeover", "open-join") and index % 3 == 1
            if mixed:
                values = ("true", "1", "false", "0", "a", "1.0", "1")
            kind = "test" if family == "filtered-match" or (family == "scalar-fold" and index % 2 == 0) else "data"
            expr = expression(rng, 1 + index % 3, kind)
            if family == "filtered-match" and index == 0:
                expr = Expr("==", "test", (read("x"), read("x")))
            if family == "arithmetic-fold":
                expr = Expr("+", "data", (read("a"), Expr("-", "data", (read("x"), integer(rng.randint(-3, 3))))))
            if family == "scalar-fold" and kind == "test":
                # A Boolean result is a final value, not a data accumulator
                # for a second integer-order step.
                values = values[:1]
            if family == "filtered-match" and mixed:
                expr = Expr("==", "test", (read("x"), integer(index % 2)))
            cases.append(Case(len(cases), family, observer, values, expr, index % 2 == 1,
                              history=4098 if family == "row-takeover" and index == 0 else 0))
    # Fixed first-class regression controls are part of every scalar cohort.
    if "scalar-fold" in families:
        comparison = Expr("<", "test", (read("a"), read("x")))
        for observer in ("value", "tuple", "bag", "catch", "ordered"):
            cases.append(Case(len(cases), "scalar-fold", observer, ("2",), comparison))
        bad = Expr("bad-add", "data", (Expr("==", "test", (read("a"), read("x"))), integer(1)))
        cases.append(Case(len(cases), "scalar-fold", "catch", ("2",), bad, negative=True))
        for boundary in (("9223372036854775807", "1"), ("-9223372036854775808", "-1")):
            cases.append(Case(len(cases), "scalar-fold", "value", boundary, Expr("+", "data", (read("a"), read("x")))))
        cases.append(Case(len(cases), "scalar-fold", "catch", ("abc",), comparison))
        tie = Expr("if", "data", (Expr("<", "test", (read("a"), read("x"))), integer(1), integer(2)))
        for observer in OBSERVERS:
            cases.append(Case(len(cases), "scalar-fold", observer, ("0",), tie))
    if "filtered-match" in families:
        cases.append(Case(len(cases), "filtered-match", "catch", ("1", "abc", "2"),
                          Expr("<", "test", (read("x"), integer(3)))))
    if "arithmetic-fold" in families:
        additive = Expr("+", "data", (read("a"), read("x")))
        for producer, values, negative in (
                ("run", ("0", "1", "1", "2"), False),
                ("moments", ("0", "1", "1", "2"), False),
                ("scan", ("9223372036854775807", "1"), False),
                ("moments", ("9223372036854775807", "1"), True),
                ("scan", ("1", "abc", "2"), True)):
            for observer in OBSERVERS:
                cases.append(Case(len(cases), "arithmetic-fold", observer, values, additive,
                                  producer=producer, negative=negative))
    if "row-takeover" in families:
        for fact_shape in ("flat", "flat-star"):
            for observer in OBSERVERS:
                cases.append(Case(len(cases), "row-takeover", observer, ("0", "0", "1", "2"), integer(0),
                                  fact_shape=fact_shape, history=4098))
    # Collective regions must also be tested after their *result* flows to
    # each observer. Direct streaming contexts above intentionally remain:
    # some workers must decline them rather than assume collection.
    collective = ("filtered-match", "grouped-count", "listed-match", "dead-rule",
                  "kept-relation", "open-join", "guard-batch", "saturation", "relaxation")
    for family in families:
        if family not in collective:
            continue
        for consumer in ("count", "exists"):
            for observer in OBSERVERS:
                values = ("0", "1", "1", "2") * (4 if family == "listed-match" else 1)
                expr = Expr("==", "test", (read("x"), read("x"))) if family == "filtered-match" else integer(0)
                cases.append(Case(len(cases), family, observer, values, expr, consumer=consumer,
                                  tagged=True, effectful=family in ("open-join", "listed-match")))
        if family in ("grouped-count", "guard-batch", "kept-relation", "open-join"):
            cases.append(Case(len(cases), family, "tuple", ("true", "1", "false", "0", "true", "1"),
                              integer(0), consumer="count", tagged=True, effectful=family == "open-join"))
        boundary = {"grouped-count": "empty-group", "guard-batch": "preheld-guard",
                    "open-join": "ground-first", "dead-rule": "incomplete-coverage"}.get(family)
        if boundary:
            for observer in OBSERVERS:
                cases.append(Case(len(cases), family, observer, ("0", "1", "1", "2"), integer(0),
                                  consumer="count", effectful=family == "open-join", tagged=True,
                                  boundary=boundary, negative=boundary == "incomplete-coverage"))
    if "match-existence" in families:
        for boundary, absent in (("ordinary", False), ("ordinary", True), ("open-existence", False)):
            for index, observer in enumerate(OBSERVERS):
                cases.append(Case(len(cases), "match-existence", observer, ("2", "3"), integer(0),
                                  nested=index % 2 == 1, negative=absent, boundary=boundary))
    return cases


def lean_controls(cases):
    lines = ["import Mettapedia.Languages.MeTTa.PeTTa.Bridges.ScalarSource",
             "open Mettapedia.Languages.MeTTa.PeTTa",
             "open Mettapedia.Languages.MeTTa.PeTTa.Bridges.ScalarSource",
             "def fixtureProgram : SpaceSemantics.Program := {equations := [], declarations := []}",
             'def fixtureEnv : Environment String := fun name => if name = "a" ∨ name = "x" then some name else none']
    for case in cases:
        if case.family != "scalar-fold":
            continue
        accepted = "false" if case.negative else "true"
        lines += [f"-- Scalar case {case.number}; source admission only, not a native execution theorem.",
                  f"example : (elaborate fixtureProgram fixtureEnv {case.expr.lean()}).isSome = {accepted} := by decide"]
    return "\n".join(lines) + "\n"
