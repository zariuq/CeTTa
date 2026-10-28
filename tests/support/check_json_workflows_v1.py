#!/usr/bin/env python3
"""Executable JSON scripting workloads and separately timed public operations.

References for workload selection (no external data required by the gate):
https://github.com/miloyip/nativejson-benchmark
https://github.com/nst/JSONTestSuite
https://jsonlines.org/
https://www.sqlite.org/json1.html
https://www.postgresql.org/docs/current/datatype-json.html
"""

import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from gslt2parse_schema_v1 import StringLiteral, Symbol
from test_lbnf_values_v1 import headed, output_values, scalar_text


def quote(text):
    # Both source-string profiles accept literal newlines. Escape only the
    # delimiters here; JSON's own escape sequences remain untouched bytes.
    return '"' + str(text).replace('\\', '\\\\').replace('"', '\\"') + '"'


def record(name, expression):
    return (f'!(let $answer {expression}\n'
            f'  (function (return (JsonWorkflowV1 {quote(name)} $answer))))\n')


def execute(binary, output, language, name, source, timeout=None):
    path = output / f"{language}-{name}.metta"
    path.write_text(source, encoding="utf-8")
    endpoint = {"petta": "petta", "prime": "prime@0.0.1"}.get(language, language)
    before = time.perf_counter_ns()
    result = subprocess.run([str(binary), "--lang", endpoint, str(path)],
                            cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    whole_ns = time.perf_counter_ns() - before
    (output / f"{language}-{name}.stdout").write_text(result.stdout)
    (output / f"{language}-{name}.stderr").write_text(result.stderr)
    if result.returncode:
        raise AssertionError((language, name, result.returncode, result.stderr[-2000:]))
    return output_values(result.stdout), whole_ns


def workflows(binary, output, languages, timeout=None):
    config = output / "config.json"
    config.write_text('{"name":"worker","limits":{"retries":2},"optional":null}')
    cases, expected = [], {}

    def check(name, expression, value):
        cases.append(record(name, expression))
        expected[name] = value

    for index, value in enumerate((None, True, False, 0, -1, 1.25, "hello λ 😀",
                                    [], {}, [1, "x", None], {"a": [1, {"b": "λ"}]})):
        wire = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        check(f"roundtrip-{index}", f"(json:stringify (json:parse {quote(wire)}))", wire)
    check("config-file", f'(json:get (json:parse (fs:read-text {quote(config)})) "name")', "worker")
    check("nested-read", f'(json:get (json:get (json:parse (fs:read-text {quote(config)})) "limits") "retries")', 2)
    pretty_value = {"name": "worker", "limits": {"retries": 2}, "optional": None}
    check("pretty-config", f'(str:sha256 (json:pretty (json:parse (fs:read-text {quote(config)}))))',
          hashlib.sha256(json.dumps(pretty_value, ensure_ascii=False, indent=2).encode()).hexdigest())
    publication = output / "published.json"
    check("write-read", f'''(let* (($config (json:parse (fs:read-text {quote(config)})))
        ($text (json:pretty $config)) ($written (fs:write {quote(publication)} $text)))
      (json:stringify (json:parse (fs:read-text {quote(publication)}))))''',
          json.dumps(pretty_value, ensure_ascii=False, separators=(",", ":")))
    response = '{"choices":[{"message":{"content":"hello"}}],"usage":{"total_tokens":12}}'
    check("llm-response", f'''(let* (($reply (json:parse {quote(response)}))
        ($choices (json:array-items (json:get $reply "choices")))
        ($first (index-atom $choices 0)))
      (json:get (json:get $first "message") "content"))''', "hello")
    check("llm-response-projected",
          f'''(json:parse-at {quote(response)} ("choices" 0 "message" "content"))''',
          "hello")
    check("project-empty-path", '(json:stringify (json:parse-at "[1,2]" ()))', '[1,2]')
    check("project-first-duplicate", '(json:parse-at "{\\"x\\":1,\\"x\\":2}" ("x"))',
          1)
    check("project-array-index", '(json:parse-at "[0,{\\"x\\":2}]" (1 "x"))',
          2)
    escaped_key = '{"a\\u03bbb":true}'
    check("project-escaped-key",
          f'(json:stringify (json:parse-at {quote(escaped_key)} ("aλb")))',
          "true")
    check("project-missing", '(json:parse-at "{\\"x\\":null}" ("y"))',
          Symbol("JsonMissingV1"))
    check("project-null", '(json:parse-at "{\\"x\\":null}" ("x"))',
          Symbol("JsonNull"))
    check("project-validates-unselected", '''(case
      (json:parse-at "{\\"x\\":1,\\"bad\\":[1,]}" ("x"))
      (((JsonFailureV1 $phase $status $call $message) "rejected")
       ($other "WRONG")))''', "rejected")
    check("project-bad-path", '''(case (json:parse-at "{\\"x\\":1}" (true))
      (((JsonFailureV1 $phase $status $call $message) "rejected")
       ($other "WRONG")))''', "rejected")

    # Independent DOM composition is the executable oracle for the selective
    # parser. These vary path depth, array position, UTF-8 and large ignored
    # siblings; equality is checked in each language lane.
    def path_source(path):
        return "(" + " ".join(quote(step) if isinstance(step, str) else str(step)
                                for step in path) + ")"

    def reference_at(wire, path):
        def descend(value, rest, depth):
            if not rest:
                return value
            step, *tail = rest
            selected = f"$__json_project_value_{depth}"
            if isinstance(step, str):
                return (f"(let {selected} (json:get {value} {quote(step)}) "
                        f"{descend(selected, tail, depth + 1)})")
            items = f"$__json_project_items_{depth}"
            return (f"(let {items} (json:array-items {value}) "
                    f"(let {selected} (index-atom {items} {step}) "
                    f"{descend(selected, tail, depth + 1)}))")

        root = "$__json_project_value_root"
        return (f"(let {root} (json:parse {quote(wire)}) "
                f"{descend(root, path, 0)})")

    for index in range(64):
        data = {
            "ignored": "λ" + ("x" * (index * 17)),
            "rows": [{"id": row, "name": f"row-{index}-{row}"}
                     for row in range(2 + index % 4)],
            "target": {"id": index, "active": index % 2 == 0},
        }
        path = (["target", "id"] if index % 3 == 0 else
                ["target", "active"] if index % 3 == 1 else
                ["rows", index % 2, "name"])
        wire = json.dumps(data, ensure_ascii=False, separators=(",", ":"))
        direct = f"(json:parse-at {quote(wire)} {path_source(path)})"
        reference = reference_at(wire, path)
        selected = data
        for step in path:
            selected = selected[step]
        selected_wire = json.dumps(
            selected, ensure_ascii=False, separators=(",", ":"))
        check(f"project-differential-{index}",
              f'(let $direct {direct} (let $reference {reference} '
              f'(if (== $direct $reference) (json:stringify $direct) "DIFF")))',
              selected_wire)
    check("missing", '(json:get-first (json:parse "{}") "x")', Symbol("JsonMissingV1"))
    check("null", '(json:get-first (json:parse "{\\"x\\":null}") "x")',
          (Symbol("JsonFoundV1"), 0, Symbol("JsonNull")))
    check("duplicate-first", '(json:get-first (json:parse "{\\"x\\":1,\\"x\\":2}") "x")',
          (Symbol("JsonFoundV1"), 0, 1))
    check("duplicate-last", '(json:get-last (json:parse "{\\"x\\":1,\\"x\\":2}") "x")',
          (Symbol("JsonFoundV1"), 1, 2))
    check("duplicate-all", '(json:get-all (json:parse "{\\"x\\":1,\\"x\\":2}") "x")',
          (Symbol("JsonMatchesV1"), (1, 2)))
    check("member-order", '(json:members (json:parse "{\\"z\\":0,\\"a\\":1}"))',
          (Symbol("JsonMembersV1"), ((Symbol("JsonPair"), "z", 0),
                                   (Symbol("JsonPair"), "a", 1))))
    check("big-integer-value", '(json:stringify (json:parse "9007199254740993"))',
          "9007199254740993")
    check("ordinary-negative-zero", '(json:stringify (json:parse "-0"))', "0")
    check("ordinary-real", '(json:stringify (json:parse "1.2300e+40"))', "1.23E40")
    for wire in ('9007199254740993', '-0', '1.2300e+40', '1e100000'):
        check("inspection-exact-number-" + wire,
              f"(json:inspect-stringify (json:inspect {quote(wire)}))", wire)
    check("ordinary-nonfinite-refused", '''(case
      (json:stringify (json:parse "1e100000"))
      (((JsonFailureV1 $phase $status $call $message) "rejected")
       ($other "WRONG")))''', "rejected")
    for name, text in (("escaped-nul", '"a\\u0000b"'),
                       ("unicode-escapes", '"\\uD834\\uDD1E"'),
                       ("unicode-separator", '"a\u2028b"')):
        emitted = json.dumps(json.loads(text), ensure_ascii=False, separators=(",", ":"))
        # U+0000 is intentionally emitted as a JSON escape, not raw NUL.
        digest = hashlib.sha256(emitted.encode()).hexdigest()
        check(name, f"(str:sha256 (json:stringify (json:parse {quote(text)})))", digest)
    check("number-array-reduction", '''(let $items (json:array-items (json:parse "[1,2,3]"))
      (foldl-atom $items 0 $sum $item (+ $sum $item)))''', 6)
    check("embedding-norm", '''(let $items (json:array-items (json:parse "[0.25,0.5,-1.0]"))
      (foldl-atom $items 0 $sum $item
        (+ $sum (* $item $item))))''', Symbol("1.3125"))
    check("nested-update", '''(let $config (json:parse "{\\"limits\\":{\\"retries\\":2},\\"name\\":\\"worker\\"}")
      (json:stringify (json-demo:with $config "limits"
        (json-demo:with (json:get $config "limits") "retries" 3))))''',
          '{"limits":{"retries":3},"name":"worker"}')
    check("append-key", '(json:stringify (json-demo:with (json:parse "{}") "x" 1))', '{"x":1}')
    check("delete-key", '(json:stringify (json-demo:without (json:parse "{\\"x\\":1,\\"y\\":2}") "x"))', '{"y":2}')
    check("row-filter", '''(let $rows (json:array-items (json:parse "[{\\"x\\":1},{\\"x\\":2},{\\"x\\":2}]"))
      (let $selected (filter-atom $rows $row (== (json:get $row "x") 2))
        (size-atom $selected)))''', 2)
    check("space-query-bag", '''(let $space (new-space)
      (let $rows (json:array-items (json:parse "[{\\"x\\":1},{\\"x\\":2},{\\"x\\":2}]"))
        (let $stored (map-atom $rows $row
          (let $x (json:get $row "x") (add-atom $space (row $x))))
          (let $answers (collapse (match $space (row 2) hit)) (size-atom $answers)))))''', 2)
    check("inspection-agreement", '''(json:stringify (json:from-inspection
      (json:inspect "{\\"x\\":1,\\"x\\":2,\\"s\\":\\"λ\\"}")))''', '{"x":1,"x":2,"s":"λ"}')
    for index, bad in enumerate(('01', '[1,]', '{"x":}', 'true false', '"\\uD800"', 'NaN')):
        check(f"reject-{index}", f'''(case (json:parse {quote(bad)})
          (((JsonFailureV1 $phase $status $call $message) "rejected") ($other "WRONG")))''', "rejected")
    check("wrong-object", '''(case (json:get (JsonArray ()) "x")
      (((JsonFailureV1 $phase $status $call $message) "rejected") ($other "WRONG")))''', "rejected")
    check("wrong-scalar", '''(case (json:stringify (InvalidJsonScalarV1 "01"))
      (((JsonFailureV1 $phase $status $call $message) "rejected") ($other "WRONG")))''', "rejected")
    check("lookup-is-not-validation", '''(json:get
      (JsonObject ((JsonPair "x" (InvalidJsonScalarV1 "01")))) "x")''',
          (Symbol("InvalidJsonScalarV1"), "01"))
    check("lookup-then-boundary-validation", '''(case
      (json:stringify (json:get
        (JsonObject ((JsonPair "x" (InvalidJsonScalarV1 "01")))) "x"))
      (((JsonFailureV1 $phase $status $call $message) "rejected")
       ($other "WRONG")))''', "rejected")
    check("jsonl-final-record", '''(let $buffer (str:line-new)
      (let (LinesCollectedV1 $lines $next) (json-demo:feed-lines $buffer "1\n2" true)
        (json-demo:parse-lines $lines)))''',
          (Symbol("LCons"), (Symbol("JsonLineV1"), 1, 1),
           (Symbol("LCons"), (Symbol("JsonLineV1"), 2, 2), Symbol("LNil"))))
    check("jsonl-crlf-seam", '''(let $buffer (str:line-new)
      (let (LinesCollectedV1 $before $pending) (json-demo:feed-lines $buffer "1\r" false)
        (let (LinesCollectedV1 $after $next) (json-demo:feed-lines $pending "\n2\r\n" true)
          (json-demo:parse-lines $after))))''',
          (Symbol("LCons"), (Symbol("JsonLineV1"), 1, 1),
           (Symbol("LCons"), (Symbol("JsonLineV1"), 2, 2), Symbol("LNil"))))
    check("jsonl-empty", '''(let $buffer (str:line-new)
      (let (LinesCollectedV1 $lines $next) (json-demo:feed-lines $buffer "" true) $lines))''', Symbol("LNil"))
    check("jsonl-chunk-seam", '''(let* (($buffer (str:line-new))
        ($text "\\"λ\u2028z\\"\n") ($first (str:byte-slice $text 0 2))
        ($rest (str:byte-slice $text 2 (str:byte-length $text))))
      (let (LinesCollectedV1 $before $pending) (json-demo:feed-lines $buffer $first false)
        (let (LinesCollectedV1 $after $next) (json-demo:feed-lines $pending $rest true)
          (let (LCons (LineV1 $line $joined) LNil) $after
            (str:sha256 (json:parse $joined))))))''',
          hashlib.sha256("λ\u2028z".encode()).hexdigest())
    check("jsonl-blank-line", '''(let $buffer (str:line-new)
      (let (LinesCollectedV1 $lines $next) (json-demo:feed-lines $buffer "\n" true)
        (let (LCons (JsonLineV1 $number $value) LNil) (json-demo:parse-lines $lines)
          (case $value (((JsonFailureV1 $p $s $c $m) "rejected") ($other "WRONG"))))))''', "rejected")

    prefix = ('!(import! &self fs)\n!(import! &self system)\n'
              '!(import! &self list)\n' +
              f'!(import! &self {quote(ROOT / "examples/json_scripting_v1.metta")})\n')
    checks = 0
    for language in languages:
        values, _ = execute(binary, output, language, "workflows", prefix + "".join(cases), timeout)
        records = [value for value in values if headed(value, "JsonWorkflowV1", 2)]
        names = [scalar_text(row[1]) for row in records]
        if len(records) != len(expected) or set(names) != set(expected):
            raise AssertionError((language, "missing/duplicate observations", names, sorted(set(expected) - set(names))))

        def plain(value):
            if isinstance(value, tuple):
                return tuple(plain(item) for item in value)
            if isinstance(value, StringLiteral):
                return scalar_text(value)
            return value

        for row in records:
            name, actual = scalar_text(row[1]), plain(row[2])
            if actual != expected[name]:
                raise AssertionError((language, name, actual, expected[name]))
            checks += 1
    print(f"JsonWorkflowSummary {checks} 0")
    return checks


def benchmarks(binary, output, repeats, sizes, timeout=None):
    """Input reading/preparation is outside each timed operation, not hidden.

    These are operation timings, not claims of feature equivalence to Python
    dictionaries: ordered duplicate-preserving object lookup is a member scan.
    """
    prefix = '!(import! &self json)\n!(import! &self fs)\n!(import! &self system)\n!(import! &self str)\n'
    observations = []
    for size in sizes:
        for shape, data in (("text", {"payload": "a" * size, "needle": 7}),
                            ("numbers", list(range(size))),
                            ("records", [{"id": i, "tag": "λ"} for i in range(size)])):
            wire = json.dumps(data, ensure_ascii=False, separators=(",", ":"))
            path = output / f"bench-{shape}-{size}.json"
            path.write_text(wire)
            for operation in ("parse", "emit", "pretty", "lookup", "project"):
                if operation in ("lookup", "project") and shape != "text":
                    continue
                body = {"parse": "(json:parse $text)", "emit": "(json:stringify $value)", "pretty": "(json:pretty $value)",
                        "lookup": '(json:get $value "needle")',
                        "project": '(json:parse-at $text ("needle"))'}[operation]
                expected = {"parse": "$value", "emit": "$text", "pretty": "(json:parse $answer)",
                            "lookup": '7', "project": '7'}[operation]
                comparison = "(== $value (json:parse $answer))" if operation == "pretty" else f"(== $answer {expected})"
                source = prefix + f'''(= (__json_benchmark_operation_v1 $text $value) {body})
                  !(let* (($text (fs:read-text {quote(path)}))
                    ($value (json:parse $text))
                    ($prepare-start (system:monotonic-ns))
                    ($warm (let $_ (__json_benchmark_operation_v1 $text $value) ()))
                    ($prepare-stop (system:monotonic-ns))
                    ($start (system:monotonic-ns))
                    ($answer (__json_benchmark_operation_v1 $text $value))
                    ($stop (system:monotonic-ns))
                    ($prepare-elapsed (- $prepare-stop $prepare-start))
                    ($elapsed (- $stop $start)) ($equal {comparison}))
                  (function (return (JsonCostV2 $prepare-elapsed $elapsed $equal))))\n'''
                preparation, times, whole = [], [], []
                for repeat in range(repeats):
                    values, elapsed = execute(binary, output, "petta", f"cost-{shape}-{size}-{operation}-{repeat}", source, timeout)
                    rows = [value for value in values if headed(value, "JsonCostV2", 3)]
                    if len(rows) != 1 or rows[0][3] != Symbol("true"):
                        raise AssertionError((shape, size, operation, "observation changed", values))
                    preparation.append(rows[0][1]); times.append(rows[0][2]); whole.append(elapsed)
                before = time.perf_counter_ns()
                for _ in range(repeats):
                    result = (json.loads(wire) if operation == "parse" else
                              json.dumps(data, ensure_ascii=False, separators=(",", ":")) if operation == "emit" else
                              json.dumps(data, ensure_ascii=False, indent=2) if operation == "pretty" else
                              json.loads(wire)["needle"] if operation == "project" else
                              data["needle"])
                python_ns = (time.perf_counter_ns() - before) // repeats
                assert result == (data if operation == "parse" else wire if operation == "emit" else
                                  json.dumps(data, ensure_ascii=False, indent=2) if operation == "pretty" else 7)
                row = dict(shape=shape, size=size, input_bytes=len(wire.encode()), operation=operation,
                           preparation_ns=preparation, median_preparation_ns=statistics.median(preparation),
                           work_ns=times, median_ns=statistics.median(times), whole_process_ns=whole,
                           python_mean_ns=python_ns)
                observations.append(row)
                print(json.dumps(row, sort_keys=True))
    (output / "benchmarks.json").write_text(json.dumps(observations, indent=2) + "\n")


def conformance(binary, output, corpus, timeout=None):
    """Run an existing JSONTestSuite-style corpus, without vendoring it.

    y_ must accept, n_ must reject, i_ may accept or reject. A crash, absent
    verdict, operational error or timeout is never counted as rejection.
    """
    source = output / "json-conformance.metta"
    source.write_text('''!(import! &self json)
!(import! &self fs)
!(import! &self system)
!(let $value (json:parse (fs:read-text (system:arg 0)))
  (case $value
    (((JsonFailureV1 JsonParseV1 $status $call $message)
       (JsonValidationV1 rejected $status))
     ($valid (JsonValidationV1 accepted JsonOkV1)))))
''')
    counts = dict(accepted=0, rejected=0, discretionary=0)
    files = sorted(path for path in corpus.glob("*.json") if path.name[:2] in ("y_", "n_", "i_"))
    if not files:
        raise AssertionError((corpus, "no JSONTestSuite-style inputs"))
    manifest = []
    for path in files:
        result = subprocess.run([str(binary), "--lang", "petta", str(source), str(path.resolve())],
                                cwd=ROOT, capture_output=True, text=True, timeout=timeout)
        records = [value for value in output_values(result.stdout) if headed(value, "JsonValidationV1", 2)]
        if result.returncode or len(records) != 1:
            raise AssertionError((path.name, "missing verdict", result.returncode, result.stdout, result.stderr))
        verdict, status = records[0][1:]
        if verdict == Symbol("rejected") and status not in (
                Symbol("JsonSyntaxRejectedV1"), Symbol("JsonInvalidUtf8V1"), Symbol("JsonInvalidUnicodeEscapeV1")):
            raise AssertionError((path.name, "not a syntax/value rejection", status))
        wanted = {"y_": Symbol("accepted"), "n_": Symbol("rejected")}.get(path.name[:2])
        if wanted is not None and verdict != wanted:
            raise AssertionError((path.name, verdict, wanted))
        counts["discretionary" if wanted is None else verdict.text] += 1
        manifest.append(dict(name=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                             verdict=verdict.text, status=status.text))
    (output / "conformance.json").write_text(json.dumps(dict(counts=counts, inputs=manifest), indent=2) + "\n")
    print(f"JsonConformanceSummary {len(files)} 0 " + json.dumps(counts, sort_keys=True))


def application_benchmarks(binary, output, repeats, sizes, timeout=None):
    """Time composed workloads, not just the codec. Ordinary loop/query and
    numeric-conversion costs are included rather than attributed to parsing.
    """
    prefix = ('!(import! &self fs)\n!(import! &self system)\n'
              '!(import! &self (library list))\n' +
              f'!(import! &self {quote(ROOT / "examples/json_scripting_v1.metta")})\n')

    def indexed_count(data):
        index = {}
        for row in data:
            index.setdefault(row["tag"], []).append("hit")
        return len(index.get("selected", ()))

    observations = []
    for size in sizes:
        rows = [{"id": i, "tag": "selected" if i % 2 == 0 else "other"} for i in range(size)]
        paths = {name: output / f"application-{name}-{size}.json" for name in ("rows", "numbers", "config", "jsonl")}
        paths["rows"].write_text(json.dumps(rows, separators=(",", ":")))
        paths["numbers"].write_text(json.dumps(list(range(size)), separators=(",", ":")))
        paths["config"].write_text(json.dumps({"name": "worker", "payload": "a" * size, "count": 0}, separators=(",", ":")))
        paths["jsonl"].write_text("\n".join(json.dumps(row, separators=(",", ":")) for row in rows))
        scenarios = [
            ("config-update", "config", '(json:stringify (json-demo:with $value "count" 1))',
             json.dumps({"name": "worker", "payload": "a" * size, "count": 1}, separators=(",", ":")),
             lambda data: json.dumps(dict(data, count=1), separators=(",", ":"))),
            ("numeric-traversal", "numbers", '''(let $items (json:array-items $value)
              (list:sum-by json-demo:number $items))''',
             sum(range(size)), lambda data: sum(data)),
            ("filter-aggregate", "rows", '''(let $items (json:array-items $value)
              (let $selected (filter-atom $items $item (== (json:get $item "tag") "selected"))
                (size-atom $selected)))''', (size + 1) // 2,
             lambda data: len([row for row in data if row["tag"] == "selected"])),
            ("space-index-query", "rows", '''(let $space (new-space)
              (let $items (json:array-items $value)
                (let $rows (map-atom $items $item
                  (let $tag (json:get $item "tag") (tag $tag)))
                  (let $_ (add-atoms $space $rows)
                    (let $matches (collapse (match $space (tag "selected") hit))
                      (size-atom $matches))))))''',
             (size + 1) // 2, indexed_count),
            ("jsonl-parse-aggregate", "jsonl", '''(foldl-atom $lines 0 $sum $line
              (+ $sum (json:parse-at $line ("id"))))''',
             sum(range(size)), lambda data: sum(json.loads(line)["id"] for line in data)),
            ("file-parse-field", "config", f'(json:parse-at (fs:read-text {quote(paths["config"])} ) ("name"))',
             "worker", lambda data: json.loads(paths["config"].read_text())["name"]),
        ]
        for name, kind, body, expected, python in scenarios:
            path = paths[kind]
            preparation = (f'($lines (str:split (str:from-codepoints (10)) $text)) ($value JsonNull)'
                           if kind == "jsonl"
                           else '($value (json:parse $text)) ($lines ())')
            wanted = quote(expected) if isinstance(expected, str) else str(expected)
            source = prefix + f'''(= (__json_workload_v1 $text $value $lines) {body})
              !(let* (($text (fs:read-text {quote(path)})) {preparation}
                ($prepare-start (system:monotonic-ns))
                ($warm (let $_ (__json_workload_v1 $text $value $lines) ()))
                ($prepare-stop (system:monotonic-ns))
                ($start (system:monotonic-ns))
                ($answer (__json_workload_v1 $text $value $lines))
                ($stop (system:monotonic-ns))
                ($prepare-elapsed (- $prepare-stop $prepare-start))
                ($elapsed (- $stop $start)) ($equal (== $answer {wanted})))
              (function (return (JsonCostV2 $prepare-elapsed $elapsed $equal))))\n'''
            preparation_timings, timings, whole = [], [], []
            for repeat in range(repeats):
                values, elapsed = execute(binary, output, "petta", f"application-{name}-{size}-{repeat}", source, timeout)
                records = [value for value in values if headed(value, "JsonCostV2", 3)]
                if len(records) != 1 or records[0][3] != Symbol("true"):
                    raise AssertionError((name, size, "observation changed", values))
                preparation_timings.append(records[0][1]); timings.append(records[0][2]); whole.append(elapsed)
            data = path.read_text().split("\n") if kind == "jsonl" else json.loads(path.read_text())
            before = time.perf_counter_ns()
            for _ in range(repeats):
                result = python(data)
            python_ns = (time.perf_counter_ns() - before) // repeats
            assert result == expected, (name, result, expected)
            observation = dict(workload=name, size=size, input_bytes=path.stat().st_size,
                               preparation_ns=preparation_timings,
                               median_preparation_ns=statistics.median(preparation_timings),
                               work_ns=timings, median_ns=statistics.median(timings),
                               whole_process_ns=whole, python_mean_ns=python_ns)
            observations.append(observation)
            print(json.dumps(observation, sort_keys=True))
    (output / "application-benchmarks.json").write_text(json.dumps(observations, indent=2) + "\n")


def projection_benchmarks(binary, output, repeats, sizes, timeout=None):
    """Selective parse: validate all bytes, materialize one small subtree."""
    prefix = '!(import! &self json)\n!(import! &self fs)\n!(import! &self system)\n'
    observations = []
    for size in sizes:
        wire = json.dumps({"payload": "a" * size, "needle": 7}, separators=(",", ":"))
        path = output / f"projection-text-{size}.json"
        path.write_text(wire)
        source = prefix + f'''(= (__json_projection_benchmark_v1 $text)
              (json:parse-at $text ("needle")))
          !(let* (($text (fs:read-text {quote(path)}))
            ($prepare-start (system:monotonic-ns))
            ($warm (let $_ (__json_projection_benchmark_v1 $text) ()))
            ($prepare-stop (system:monotonic-ns))
            ($start (system:monotonic-ns))
            ($answer (__json_projection_benchmark_v1 $text))
            ($stop (system:monotonic-ns))
            ($prepare-elapsed (- $prepare-stop $prepare-start))
            ($elapsed (- $stop $start))
            ($equal (== $answer 7)))
          (function (return (JsonCostV2 $prepare-elapsed $elapsed $equal))))\n'''
        preparation, timings, whole = [], [], []
        for repeat in range(repeats):
            values, elapsed = execute(
                binary, output, "petta", f"projection-{size}-{repeat}",
                source, timeout)
            records = [value for value in values if headed(value, "JsonCostV2", 3)]
            if len(records) != 1 or records[0][3] != Symbol("true"):
                raise AssertionError(("projection", size, "observation changed", values))
            preparation.append(records[0][1]); timings.append(records[0][2]); whole.append(elapsed)
        before = time.perf_counter_ns()
        for _ in range(repeats):
            result = json.loads(wire)["needle"]
        python_ns = (time.perf_counter_ns() - before) // repeats
        assert result == 7
        observation = dict(
            workload="selective-parse", size=size,
            input_bytes=len(wire.encode()), preparation_ns=preparation,
            median_preparation_ns=statistics.median(preparation), work_ns=timings,
            median_ns=statistics.median(timings),
            whole_process_ns=whole, python_mean_ns=python_ns)
        observations.append(observation)
        print(json.dumps(observation, sort_keys=True))
    (output / "projection-benchmarks.json").write_text(
        json.dumps(observations, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/json-workflows-v1")
    parser.add_argument("--language", action="append", choices=("he", "petta", "prime"))
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--sizes", type=int, nargs="+", default=[1024, 4096])
    parser.add_argument("--workflow-sizes", type=int, nargs="+", default=[128, 512, 2048])
    parser.add_argument("--projection-sizes", type=int, nargs="+", default=[4096, 1048576])
    parser.add_argument("--conformance", type=Path, help="existing JSONTestSuite test_parsing directory")
    parser.add_argument("--timeout-seconds", type=float, help="optional caller-selected subprocess deadline")
    args = parser.parse_args()
    if args.repeats < 1 or any(size < 1 for size in
                               args.sizes + args.workflow_sizes + args.projection_sizes):
        parser.error("positive repeat count and sizes required")
    if args.timeout_seconds is not None and args.timeout_seconds <= 0:
        parser.error("positive timeout required when supplied")
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    workflows(args.cetta.resolve(), output, args.language or ("he", "petta", "prime"), args.timeout_seconds)
    if args.benchmark:
        benchmarks(args.cetta.resolve(), output, args.repeats, args.sizes, args.timeout_seconds)
        application_benchmarks(args.cetta.resolve(), output, args.repeats, args.workflow_sizes, args.timeout_seconds)
        projection_benchmarks(args.cetta.resolve(), output, args.repeats,
                              args.projection_sizes, args.timeout_seconds)
    if args.conformance:
        conformance(args.cetta.resolve(), output, args.conformance.resolve(), args.timeout_seconds)


if __name__ == "__main__":
    main()
