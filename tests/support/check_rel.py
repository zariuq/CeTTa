"""Independent SQLite ordered-occurrence and sequential-fold oracles."""
import argparse
import itertools
import json
import random
import re
import sqlite3
import time
import os
from pathlib import Path
import struct
import subprocess
import tempfile


class Native:
    def __init__(self, binary, directory):
        self.bin = Path(binary).resolve(strict=True)
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.records = []

    def run(self, source, label, timeout=90):
        path = Path(tempfile.mkdtemp(prefix=label + "-", dir=self.directory))
        for part in ("home", "config", "cache", "data", "state", "runtime", "tmp", "cwd"):
            (path / part).mkdir()
        env = {"PATH": os.defpath, "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8",
               "HOME": str(path / "home"), "TMPDIR": str(path / "tmp"),
               "XDG_CONFIG_HOME": str(path / "config"), "XDG_CACHE_HOME": str(path / "cache"),
               "XDG_DATA_HOME": str(path / "data"), "XDG_STATE_HOME": str(path / "state"),
               "XDG_RUNTIME_DIR": str(path / "runtime")}
        script = path / "input.metta"
        script.write_text(source)
        command = [str(self.bin), "--lang", "petta", "--import-mode", "relative",
                   "--run-report", str(path / "run.json"), str(script)]
        result = subprocess.run(command, cwd=path / "cwd", env=env, input="",
                                capture_output=True, text=True, timeout=timeout)
        (path / "stdout.txt").write_text(result.stdout)
        (path / "stderr.txt").write_text(result.stderr)
        self.records.append({"label": label, "exit": result.returncode})
        return result.returncode, result.stdout, result.stderr, path

    def finish(self):
        (self.directory / "children.json").write_text(json.dumps(self.records, indent=2))


def import_rel():
    return "!(import! &self (library rel))\n"



class Text(str):
    pass


def emit(value):
    if isinstance(value, (tuple, list)):
        return "(" + " ".join(map(emit, value)) + ")"
    if isinstance(value, Text):
        return json.dumps(value, ensure_ascii=False)
    return str(value)


def parse(text):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', text)
    position = 0

    def atom():
        nonlocal position
        token = tokens[position]
        position += 1
        if token == "(":
            result = []
            while tokens[position] != ")":
                result.append(atom())
            position += 1
            return result
        if token.startswith('"'):
            return Text(json.loads(token))
        if re.fullmatch(r"[-+]?\d+", token):
            return int(token)
        if re.fullmatch(r"[-+]?(?:\d+\.\d*|\d*\.\d+|\d+[eE][-+]?\d+)(?:[eE][-+]?\d+)?", token):
            return float(token)
        return token

    result = []
    while position < len(tokens):
        result.append(atom())
    return result


def same(actual, expected):
    """Atom-kind-sensitive equality, including Float versus Int and String."""
    if type(actual) is not type(expected):
        return False
    if isinstance(actual, list):
        return len(actual) == len(expected) and all(same(a, b) for a, b in zip(actual, expected))
    if isinstance(actual, float):
        return struct.pack(">d", actual) == struct.pack(">d", expected)
    return actual == expected


PRELUDE = '''
; Adapters deliberately restrict comparison to closed integer keys. NULL
; does not match in SQL predicates, but is one class for DISTINCT/GROUP BY.
(= (fixture:key $row) (index-atom $row 1))
(= (fixture:equiv $left $right) (== $left $right))
(= (fixture:same-key $left $right)
   (let* (($a (fixture:key $left)) ($b (fixture:key $right))) (== $a $b)))
(= (fixture:predicate $row)
   (case (fixture:key $row)
      ((RelNull RelUnknown)
       ((RelValue $n) (if (> $n 1) RelTrue RelFalse)))))
(= (fixture:join $left $right)
   (let* (($a (fixture:key $left)) ($b (fixture:key $right)))
     (if (or (== $a RelNull) (== $b RelNull)) RelUnknown
       (if (== $a $b) RelTrue RelFalse))))
(= (fixture:pair $left $right)
   (let* (($a (index-atom $left 0)) ($b (index-atom $right 0))) (Pair $a $b)))
(= (fixture:project $row)
   (let* (($id (index-atom $row 0)) ($label (index-atom $row 3))) (Projected $id $label)))
(= (fixture:less $left $right)
   (let* (($a (index-atom $left 2)) ($b (index-atom $right 2))) (< $a $b)))
(= (fixture:add $acc $row) (let $value (index-atom $row 2) (+ $acc $value)))
(= (fixture:collect $acc $row)
   (let $id (index-atom $row 0) (list:push $acc $id)))
(= (fixture:identity $row) $row)
(= (fixture:true $row) RelTrue)
(= (fixture:join-true $left $right) RelTrue)
(= (fixture:left $left $right) $left)
(= (fixture:false-less $left $right) False)
(= (fixture:never-equivalent $left $right) False)
(= (fixture:constant-key $row) key)
'''


def row_data(rows):
    return [[r[0], "RelNull" if r[1] is None else ["RelValue", r[1]], r[2], Text(r[3])]
            for r in rows]


class Suite:
    def __init__(self):
        self.cases = []
        self.reference_ns = 0
        self.db = sqlite3.connect(":memory:")
        self.db.executescript("CREATE TABLE l(occ INTEGER PRIMARY KEY, id INTEGER, k INTEGER, v REAL, label TEXT);"
                              "CREATE TABLE r(occ INTEGER PRIMARY KEY, id INTEGER, k INTEGER, v REAL, label TEXT);")
        self.generated_pairs = 0

    def add(self, label, call, expected):
        self.cases.append((label, call, expected))

    def fragment(self, label, left, right):
        start = time.perf_counter_ns()
        self.db.execute("DELETE FROM l")
        self.db.execute("DELETE FROM r")
        self.db.executemany("INSERT INTO l VALUES(?,?,?,?,?)", [(i, *row) for i, row in enumerate(left)])
        self.db.executemany("INSERT INTO r VALUES(?,?,?,?,?)", [(i, *row) for i, row in enumerate(right)])
        l, r = row_data(left), row_data(right)
        ql, qr = "(quote " + emit(l) + ")", "(quote " + emit(r) + ")"

        def rows(query):
            return row_data(self.db.execute(query).fetchall())

        self.add(label + "-select", f"(rel:select fixture:predicate {ql})",
                 rows("SELECT id,k,v,label FROM l WHERE k>1 ORDER BY occ"))
        self.add(label + "-project", f"(rel:project fixture:project {ql})",
                 [["Projected", x[0], Text(x[1])] for x in self.db.execute("SELECT id,label FROM l ORDER BY occ")])
        self.add(label + "-union", f"(rel:union-all {ql} {qr})",
                 rows("SELECT id,k,v,label FROM (SELECT 0 side,* FROM l UNION ALL SELECT 1 side,* FROM r) ORDER BY side,occ"))
        self.add(label + "-inner", f"(rel:inner-join fixture:join fixture:pair {ql} {qr})",
                 [["Pair", *x] for x in self.db.execute("SELECT l.id,r.id FROM l JOIN r ON l.k=r.k ORDER BY l.occ,r.occ")])
        for family, exists in (("semi", "EXISTS"), ("anti", "NOT EXISTS")):
            self.add(label + "-" + family, f"(rel:{family}-join fixture:join {ql} {qr})",
                     rows(f"SELECT id,k,v,label FROM l WHERE {exists}(SELECT 1 FROM r WHERE l.k=r.k) ORDER BY occ"))
        self.add(label + "-exists", f"(rel:exists fixture:predicate {ql})",
                 "RelTrue" if self.db.execute("SELECT EXISTS(SELECT 1 FROM l WHERE k>1)").fetchone()[0] else "RelFalse")
        self.add(label + "-distinct", f"(rel:distinct fixture:same-key {ql})",
                 rows("SELECT id,k,v,label FROM l WHERE occ IN(SELECT MIN(occ) FROM l GROUP BY k) ORDER BY occ"))
        self.add(label + "-order", f"(rel:order fixture:less {ql})",
                 rows("SELECT id,k,v,label FROM l ORDER BY v,occ"))
        # Large integers here exercise parameters beyond every generated size.
        for offset, count in ((0, 0), (0, 2), (1, 3), (2, 1000000), (1000000, 2)):
            self.add(f"{label}-page-{offset}-{count}", f"(rel:page {offset} {count} {ql})",
                     rows(f"SELECT id,k,v,label FROM l ORDER BY occ LIMIT {count} OFFSET {offset}"))
        groups, group_ids, running, total = {}, {}, [], 0.0
        for row in left:
            k = row[1]
            if k not in groups:
                groups[k] = 0.0
                group_ids[k] = []
            groups[k] = groups[k] + row[2]
            group_ids[k] = group_ids[k] + [row[0]]
            total = total + row[2]
            running.append(total)
        key = lambda k: "RelNull" if k is None else ["RelValue", k]
        self.add(label + "-group", f"(rel:group-fold fixture:key fixture:equiv 0.0 fixture:add {ql})",
                 [["RelGroup", key(k), v] for k, v in groups.items()])
        self.add(label + "-independent-initial", f"(rel:group-fold fixture:key fixture:equiv (quote (Seed)) fixture:collect {ql})",
                 [["RelGroup", key(k), ["Seed", *ids]] for k, ids in group_ids.items()])
        self.add(label + "-running", f"(rel:running-fold 0.0 fixture:add {ql})", running)
        self.reference_ns += time.perf_counter_ns() - start

    def truth(self):
        truth = [(1, "RelTrue"), (0, "RelFalse"), (None, "RelUnknown")]
        name = lambda x: {1: "RelTrue", 0: "RelFalse", None: "RelUnknown"}[x]
        for sql, a in truth:
            self.add("not-" + a, f"(rel:not {a})", name(self.db.execute("SELECT NOT ?", (sql,)).fetchone()[0]))
        for (sa, a), (sb, b) in itertools.product(truth, repeat=2):
            for op in ("and", "or"):
                val = self.db.execute(f"SELECT ? {op} ?", (sa, sb)).fetchone()[0]
                self.add(f"{op}-{a}-{b}", f"(rel:{op} {a} {b})", name(val))

    def failures(self):
        q = "(quote ((0 (RelValue 2) 1.0 x) (1 (RelValue 2) 2.0 y)))"
        roles = [
            ("select", f"(rel:select FAIL {q})", 1, "truth"),
            ("project", f"(rel:project FAIL {q})", 1, "value"),
            ("inner-predicate", f"(rel:inner-join FAIL fixture:pair {q} {q})", 2, "truth"),
            ("inner-projection", f"(rel:inner-join fixture:join-true FAIL {q} {q})", 2, "value"),
            ("semi", f"(rel:semi-join FAIL {q} {q})", 2, "truth"),
            ("anti", f"(rel:anti-join FAIL {q} {q})", 2, "truth"),
            ("exists", f"(rel:exists FAIL {q})", 1, "truth"),
            ("distinct", f"(rel:distinct FAIL {q})", 2, "bool"),
            ("order", f"(rel:order FAIL {q})", 2, "bool"),
            ("group-key", f"(rel:group-fold FAIL fixture:equiv 0.0 fixture:add {q})", 1, "value"),
            ("group-equivalent", f"(rel:group-fold fixture:key FAIL 0.0 fixture:add {q})", 2, "bool"),
            ("group-step", f"(rel:group-fold fixture:key fixture:equiv 0.0 FAIL {q})", 2, "value"),
            ("running", f"(rel:running-fold 0.0 FAIL {q})", 2, "value"),
        ]
        for role, call, arity, kind in roles:
            for failure in ("none", "raise", "error", "many", "invalid"):
                if failure == "invalid" and kind == "value":
                    continue
                f = f"fixture:{failure}{arity}"
                if failure in ("none", "raise", "many"):
                    answers = ["RelTrue", "RelFalse"] if failure == "many" else []
                    expected = ["Error", ["RelCallback", f], ["RelCallbackAnswers", answers]]
                elif failure == "error":
                    data = ["Error", "Callback", "Broken"]
                    if kind == "value":
                        expected = {
                            "project": [data, data], "inner-projection": [data]*4,
                            "group-key": [["RelGroup", data, 3.0]],
                            "group-step": [["RelGroup", ["RelValue", 2], data]],
                            "running": [data, data]}[role]
                    else:
                        expected = ["Error", "RelPredicate" if kind == "truth" else "RelComparison",
                                    ["RelExpectedTruth" if kind == "truth" else "RelExpectedBool", data]]
                else:
                    expected = ["Error", "RelPredicate" if kind == "truth" else "RelComparison",
                                ["RelExpectedTruth" if kind == "truth" else "RelExpectedBool", "Wrong"]]
                prefix = "value-data" if kind == "value" and failure == "error" else "failure"
                self.add(f"{prefix}-{role}-{failure}", call.replace("FAIL", f), expected)

    def inert(self):
        rows = [["fixture:sentinel"], ["+", 1, 2], ["Error", "Raw", "Data"],
                ["Payload", ["Error", "Nested", "Data"]], ["RelOk", "data"],
                ["add-atom", "&self", "SentinelExecuted"], [], ["quote", ["fixture:sentinel"]],
                ["fixture:sentinel"]]
        q = "(quote " + emit(rows) + ")"
        for call in (f"(rel:select fixture:true {q})", f"(rel:project fixture:identity {q})",
                     f"(rel:union-all {q} ())", f"(rel:order fixture:false-less {q})",
                     f"(rel:distinct fixture:never-equivalent {q})", f"(rel:page 0 99 {q})",
                     f"(rel:inner-join fixture:join-true fixture:left {q} (quote ((right))))",
                     f"(rel:semi-join fixture:join-true {q} (quote ((right))))",
                     f"(rel:anti-join fixture:join-true {q} ())"):
            self.add("inert-" + str(len(self.cases)), call, rows)
        self.add("inert-running", f"(rel:running-fold () fixture:append-row {q})",
                 [rows[:i+1] for i in range(len(rows))])
        self.add("inert-group", f"(rel:group-fold fixture:identity fixture:equiv () fixture:append-row {q})",
                 [["RelGroup", row, [row] * rows.count(row)] for i, row in enumerate(rows) if row not in rows[:i]])
        raw = [["Error", "Raw", "Row"], ["fixture:sentinel"]]
        qr = "(quote " + emit(raw) + ")"
        for call in (f"(rel:select fixture:true {qr})", f"(rel:union-all {qr} ())",
                     f"(rel:order fixture:false-less {qr})", f"(rel:page 0 99 {qr})"):
            self.add("raw-error-data-" + str(len(self.cases)), call, raw)
        self.add("sentinel-absent", "(collapse (match &self SentinelExecuted executed))", [])
        # Prove the sentinel itself is operational, only after all inert tests.
        self.add("sentinel-live-control", "(fixture:sentinel)", "true")
        self.add("sentinel-present-control", "(collapse (match &self SentinelExecuted executed))", ["executed"])

    def adversarial(self):
        # A caller-supplied equivalence is not silently replaced with ==.
        rows = [[0, ["RelValue", 3], 1.0, Text("odd-first")],
                [1, ["RelValue", 4], 1.0, Text("even-first")],
                [2, ["RelValue", 1], 1.0, Text("odd-again")],
                [3, ["RelValue", 2], 1.0, Text("even-again")]]
        q = "(quote " + emit(rows) + ")"
        self.add("custom-equivalence-distinct", f"(rel:distinct fixture:parity-rows {q})", rows[:2])
        self.add("custom-equivalence-group", f"(rel:group-fold fixture:key fixture:parity () fixture:collect {q})",
                 [["RelGroup", ["RelValue", 3], [0, 2]], ["RelGroup", ["RelValue", 4], [1, 3]]])
        unicode_rows = [[i, ["RelValue", Text(k)], 1.0, Text("label")]
                        for i, k in enumerate(["é", "é", "猫", "é", "猫", "é"])]
        q = "(quote " + emit(unicode_rows) + ")"
        self.add("unicode-key-distinct", f"(rel:distinct fixture:same-key {q})", unicode_rows[:3])
        self.add("unicode-key-groups", f"(rel:group-fold fixture:key fixture:equiv () fixture:collect {q})",
                 [["RelGroup", ["RelValue", Text(k)], ids]
                  for k, ids in (("é", [0, 3]), ("é", [1, 5]), ("猫", [2, 4]))])
        # Failure after an earlier successful output must replace, not truncate,
        # the whole result. Boolean equality/order fail at a reached comparison.
        for family, call in (
            ("select", f"(rel:select fixture:late-truth {q})"),
            ("project", f"(rel:project fixture:late-value {q})"),
            ("inner", f"(rel:inner-join fixture:late-join fixture:left {q} (quote ((right))))"),
            ("semi", f"(rel:semi-join fixture:late-join {q} (quote ((right))))"),
            ("anti", f"(rel:anti-join fixture:late-no-join {q} (quote ((right))))"),
            ("exists", f"(rel:exists fixture:late-no-truth {q})"),
            ("distinct", f"(rel:distinct fixture:late-bool {q})"),
            ("order", f"(rel:order fixture:late-bool {q})"),
            ("group", f"(rel:group-fold fixture:constant-key fixture:equiv () fixture:late-step {q})"),
            ("running", f"(rel:running-fold () fixture:late-step {q})")):
            callback = {"select": "late-truth", "project": "late-value", "inner": "late-join",
                        "semi": "late-join", "anti": "late-no-join", "exists": "late-no-truth",
                        "distinct": "late-bool", "order": "late-bool", "group": "late-step", "running": "late-step"}[family]
            self.add("late-failure-" + family, call,
                     ["Error", ["RelCallback", "fixture:" + callback], ["RelCallbackAnswers", []]])
        self.add("carrier-error-suffix-running",
                 "(rel:running-fold start fixture:second (quote ((0 prefix) (1 Error) (2 source) (3 reason))))",
                 ["prefix", "Error", "source", "reason"])
        values = [["RelOk", "data"], ["Error", ["RelCallback", "data"], ["RelCallbackAnswers", []]], []]
        q = "(quote " + emit([[i, value] for i, value in enumerate(values)]) + ")"
        self.add("carrier-shaped-accumulators", f"(rel:running-fold () fixture:second {q})", values)
        self.add("carrier-shaped-projection", f"(rel:project fixture:second {q})", values)
        self.add("error-initial-group", "(rel:group-fold fixture:key fixture:equiv (quote (Error initial value)) fixture:keep-acc (quote ((0 a) (1 b) (2 a))))",
                 [["RelGroup", "a", ["Error", "initial", "value"]], ["RelGroup", "b", ["Error", "initial", "value"]]])


FAILURES = "\n".join(
    f"(= (fixture:{kind}{arity} {'$a' if arity == 1 else '$a $b'}) {body})"
    for arity in (1, 2) for kind, body in
    (("none", "(empty)"), ("raise", "(/ 1 0)"), ("error", "(Error Callback Broken)"),
     ("many", "(superpose (RelTrue RelFalse))"), ("invalid", "Wrong")))
INERT = '''
(= (fixture:sentinel) (add-atom &self SentinelExecuted))
(= (fixture:append-row $acc $row) (list:push $acc $row))
(= (fixture:parity (RelValue $a) (RelValue $b)) (== (% $a 2) (% $b 2)))
(= (fixture:parity-rows $a $b)
   (let* (($ka (fixture:key $a)) ($kb (fixture:key $b))) (fixture:parity $ka $kb)))
(= (fixture:late-truth $row)
   (if (== (index-atom $row 0) 0) RelTrue (empty)))
(= (fixture:late-no-truth $row)
   (if (== (index-atom $row 0) 0) RelFalse (empty)))
(= (fixture:late-value $row)
   (if (== (index-atom $row 0) 0) $row (empty)))
(= (fixture:late-join $a $b) (fixture:late-truth $a))
(= (fixture:late-no-join $a $b) (fixture:late-no-truth $a))
(= (fixture:late-bool $a $b) (empty))
(= (fixture:late-step $acc $row)
   (if (== (index-atom $row 0) 0) (list:push $acc $row) (empty)))
(= (fixture:second $row) (index-atom $row 1))
(= (fixture:second $acc $row) (index-atom $row 1))
(= (fixture:keep-acc $acc $row) $acc)
'''


def run_cases(runner, cases, label):
    source = import_rel() + PRELUDE + FAILURES + INERT
    for i, (name, call, expected) in enumerate(cases):
        source += f"\n!(let $answers (collapse {call}) (Case {i} $answers))\n"
    code, stdout, stderr, path = runner.run(source, label, timeout=90)
    (path / "expected.json").write_text(json.dumps(cases, indent=2, ensure_ascii=False))
    results = parse(stdout)
    expected = ["true"] + [["Case", i, [value]] for i, (_, _, value) in enumerate(cases)]
    mismatches = []
    if not same(results, expected):
        for i, (want, got) in enumerate(itertools.zip_longest(expected, results)):
            if not same(got, want):
                mismatches.append(dict(index=i, label=cases[i-1][0] if i and i <= len(cases) else "import",
                                       expected=want, actual=got))
    report = json.loads((path / "run.json").read_text())
    good = (code == 0 and not mismatches and report["queries_unsettled"] == 0
            and len(report["queries"]) == len(expected)
            and all(q["observation_complete"] and q["answers"] == 1 and not q["faults"] for q in report["queries"]))
    (path / "assertions.json").write_text(json.dumps(dict(pass_gate=good, mismatches=mismatches), indent=2, ensure_ascii=False))
    if not good:
        print(json.dumps(mismatches[:8], indent=2, ensure_ascii=False))
        raise AssertionError(f"{label}: native complete-answer gate failed; exit={code}; {stderr}; evidence={path}")
    return path


def make_suite(generated=32, seed=20261026):
    suite = Suite()
    suite.truth()
    fixtures = [([], []), ([(0, None, 0., "空")], []),
                ([], [(10, 2, 1., "x")]),
                ([(0, 2, 1., "Žluťoučký 🐈"), (1, None, 2., "NULL"), (2, 2, 1., "é"),
                  (0, 2, 1., "Žluťoučký 🐈"), (3, -1, 3., "é"), (4, 9, -1., "missing")],
                 [(10, 2, 1., "猫"), (11, 2, 1., "two"), (12, None, 1., "null"), (13, 7, 1., "missing")]),
                ([(0, 1, 1e16, "a"), (1, 2, 3., "b"), (2, 1, 1., "c"),
                  (3, 2, -3., "d"), (4, 1, -1e16, "e"), (5, 1, 2., "f")], [])]
    for i, (left, right) in enumerate(fixtures):
        suite.fragment(f"fixture-{i}", left, right)
    rng = random.Random(seed)
    for case in range(generated):
        def generate(side):
            result = []
            for i in range(rng.randrange(0, 9)):
                if result and rng.randrange(4) == 0:
                    result.append(rng.choice(result))
                else:
                    result.append((100*side+i, rng.choice([None, -1, 0, 1, 2, 2, 3]),
                                   float(rng.randrange(-3, 4)), rng.choice(["猫", "a", "é", "é", ""])))
            return result
        suite.fragment(f"generated-{case}", generate(0), generate(1))
        suite.generated_pairs += 1
    suite.failures()
    suite.adversarial()
    suite.inert()
    return suite


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--cetta", required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--seed", type=int, default=20261026)
    p.add_argument("--generated", type=int, default=32)
    args = p.parse_args()
    runner = Native(args.cetta, args.output)
    suite = make_suite(args.generated, args.seed)
    try:
        # Modest batches keep parser/catalogue size and memory bounded.
        inert_start = next(i for i, (name, _, _) in enumerate(suite.cases) if name.startswith("inert-"))
        for start in range(0, inert_start, 90):
            run_cases(runner, suite.cases[start:min(start+90, inert_start)], f"cases-{start}")
        run_cases(runner, suite.cases[inert_start:], "inert-sentinels")
        summary = dict(passed=len(suite.cases), generated_pairs=suite.generated_pairs,
                       fixture_pairs=5, seed=args.seed, generated_max_rows_per_side=8,
                       sqlite_version=sqlite3.sqlite_version, reference_generation_ns=suite.reference_ns)
        (runner.directory / "summary.json").write_text(json.dumps(summary, indent=2))
        print(json.dumps(summary, indent=2))
    finally:
        suite.db.close()
        runner.finish()


if __name__ == "__main__":
    main()
