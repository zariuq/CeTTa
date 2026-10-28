#!/usr/bin/env python3
"""Independent source templates, LC1 bytes and executable helper boundaries."""

from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import itertools
import json
from pathlib import Path
import struct
import subprocess

from check_binary_codec import ROOT, encode as frame, observation, parse_forms, tensor
from check_binary_tree import packed
from check_ic_archive import q, syntax, uint
from check_ic_search import check_controls
from check_source_codec import encode as st1, encode_value, reference, text_arg


OPS = {(head, 2) for head in ("+", "-", "*", "/", "%", "==", "<", ">", "<=", ">=", "and", "or",
                              "cons-atom", "union-atom", "index-atom")}
OPS |= {("if", 3), ("not", 1), ("superpose", 1), ("car-atom", 1), ("cdr-atom", 1),
        ("size-atom", 1), ("println!", 1)}
SCALARS = {"I64", "F64", "BigInt", "Rational", "Bool", "String"}
INTRINSICS = {head for head, _arity in OPS} | {"quote", "=", ":", "let", "let*", "case", "match", "eval"}


def symbol(name):
    return ["Syntax:Literal", "Symbol", name.encode()]


def is_symbol(term, name):
    return term == symbol(name)


def admitted(term, count, quoted=False):
    if term[0] == "Syntax:Variable":
        return 0 <= int(term[1]) < count
    if term[0] == "Syntax:Literal":
        return not is_symbol(term, "&self")
    children = term[1]
    if not children:
        return True
    if quoted:
        return all(admitted(child, count, True) for child in children)
    head, *tail = children
    if head[:2] == ["Syntax:Literal", "Symbol"]:
        name = head[2].decode()
        if name == "quote" and len(tail) == 1:
            return all(admitted(child, count, True) for child in tail)
        if name == "superpose" and len(tail) == 1:
            answers = tail[0]
            return (answers[0] == "Syntax:Expr" and all(admitted(child, count) for child in answers[1])) or (
                answers[0] == "Syntax:Variable" and 0 <= int(answers[1]) < count)
        return (name, len(tail)) in OPS and all(admitted(child, count) for child in tail)
    return head[0] == "Syntax:Literal" and all(admitted(child, count) for child in children)


def clause(form):
    if form[0] != "Syntax:Form" or form[2][0] != "Syntax:Expr":
        return None
    parts = form[2][1]
    if len(parts) != 3 or not is_symbol(parts[0], "=") or parts[1][0] != "Syntax:Expr":
        return None
    left = parts[1][1]
    if not left or left[0][:2] != ["Syntax:Literal", "Symbol"]:
        return None
    if left[0][2].decode() in INTRINSICS:
        return None
    if any(name[0] != "Syntax:PlainName" for name in form[1]):
        return None
    if left[1:] != [["Syntax:Variable", str(i)] for i in range(len(form[1]))]:
        return None
    return left, parts[2]


def sites(document):
    annotations = []
    for form in document[3]:
        if form[2][0] == "Syntax:Expr" and len(form[2][1]) == 3 and is_symbol(form[2][1][0], ":"):
            annotations.append(form[2][1][1])
    return [(i, *found) for i, form in enumerate(document[3]) if (found := clause(form))
            and found[0][0] not in annotations and admitted(found[1], len(form[1]))]


def lc1(name, document, version=b"LC1"):
    payload = packed(("node", uint(9), [("leaf", version), ("leaf", name.encode()),
                                       encode_value("Syntax:Document", document)]))
    return frame([b"SIC1", b"LC1", payload])


def plain(document):
    return frame([b"SIC1", b"ST1", st1(document)])


def candidate(document, name, left_index=0, right_index=1):
    """Generalize source ASTs directly, independently of the ranked byte-tree IR."""
    chosen = sites(document)
    if not (0 <= left_index < len(chosen) and 0 <= right_index < len(chosen)):
        return None
    _left_origin, _left_head, left = chosen[left_index]
    _right_origin, _right_head, right = chosen[right_index]
    pairs = []

    def pattern(a, b):
        if a[0] == b[0] == "Syntax:Expr" and len(a[1]) == len(b[1]):
            return ["Syntax:Expr", [pattern(x, y) for x, y in zip(a[1], b[1])]]
        if a[0] == b[0] == "Syntax:Literal" and a == b:
            return deepcopy(a)
        key = a, b
        if key not in pairs:
            pairs.append(deepcopy(key))
        return ["Hole", pairs.index(key)]

    learned = pattern(left, right)

    def compile_body(tree):
        if tree[0] == "Hole":
            return ["Syntax:Variable", str(tree[1])]
        if tree[0] == "Syntax:Expr":
            return ["Syntax:Expr", list(map(compile_body, tree[1]))]
        return deepcopy(tree)

    body = compile_body(learned)
    if not admitted(body, len(pairs)):
        return None

    def match(tree, value, env):
        if tree[0] == "Hole":
            key = tree[1]
            if value[0] not in ("Syntax:Variable", "Syntax:Literal"):
                return False
            if value[0] == "Syntax:Literal" and value[1] not in SCALARS:
                return False
            if key in env and env[key] != value:
                return False
            env[key] = value
            return True
        if tree[0] == "Syntax:Expr":
            return value[0] == tree[0] and len(tree[1]) == len(value[1]) and all(
                match(p, v, env) for p, v in zip(tree[1], value[1]))
        return tree == value

    result = deepcopy(document)
    count = 0
    for origin, _head, source in chosen:
        env = {}
        if match(learned, source, env):
            result[3][origin][2][1][2] = ["Syntax:Expr", [symbol(name)] + [env[i] for i in range(len(pairs))]]
            count += 1
    if count < 2:
        return None
    names = [["Syntax:PlainName", q("p" + str(i))] for i in range(len(pairs))]
    head = ["Syntax:Expr", [symbol(name)] + [["Syntax:Variable", str(i)] for i in range(len(pairs))]]
    definition = ["Syntax:Form", names, ["Syntax:Expr", [symbol("="), head, body]]]
    result[3].insert(0, definition)
    return result, count


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT / "lib/petta/lib_ic_library.metta").read_text())
    code = ['!(import! &self (library lib_ic_library))', '''
(= (library-test:factor $text)
   (let* (($document (syntax:read $text)) ($prepared (ic:prepare-library $document)))
     (case (source-library:seed $prepared "ic-learned:test" 0 1)
       (((Library:Candidate $bytes $support)
         (LibraryObservation $support (tensor:inspect $bytes)
           (== (ic:library-unpack $bytes) (IC:Some $document))
           (case (ic:library-check (ic:source-pack $document) (ic:source-pack $document) $bytes)
             (((IC:PackedAccepted $value $before $after) (Saved $before $after))
              ((IC:PackedRejected $why) $why)))))
        ($other $other)))))
(= (library-test:export $text)
   (let $prepared (ic:prepare-library (syntax:read $text))
     (case (source-library:seed $prepared "ic-learned:test" 0 1)
       (((Library:Candidate $bytes $support)
         (case (ic:export-library $bytes)
           (((Library:Export $name $document $source $original) (tensor:inspect (binary:utf8 $source)))
            ($other $other))))
        ($other $other)))))
(= (library-test:finish $state $remaining)
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $stats BudgetExhausted $fault $next)
       (if (> $remaining 0) (library-test:finish $next (- $remaining 1))
         (let $released (ic:release $next) (LibraryTestFault Budget))))
      ($other $other))))
(= (library-test:search $text $pieces)
   (case (ic:learn-library (syntax:read $text) "test" 64)
     (((IC:PreparedSearch $state)
       (let $result (if $pieces (library-test:finish $state 1000) (ic:resume $state 1000))
         (case $result
           (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $next)
             (LibrarySearch (tensor:inspect $best) $seen $accepted $faults $reason))
            ($other $other)))))
      ($other $other))))
(= (library-test:lifecycle $text)
   (case (ic:learn-library (syntax:read $text) "test" 64)
     (((IC:PreparedSearch $state)
       (case (ic:resume $state 0)
         (((IC:SearchResult $initial $stats $pause $fault $next)
           (case (ic:cancel $next)
             (((IC:SearchResult $same $same-stats $cancelled $same-fault $later)
               (case (ic:resume $later 1000)
                 (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $finished)
                   (LibraryLifecycle $pause $cancelled (tensor:inspect $best)
                     $seen $accepted $faults $reason))
                  ($other $other))))
              ($other $other))))
          ($other $other))))
      ($other $other))))
(= (library-test:changed-source $text)
   (case (ic:learn-library (syntax:read $text) "test" 64)
     (((IC:PreparedSearch $state)
       (let* (($changed (add-atom &self LibraryTestRevisionProbe))
              ($result (ic:resume $state 0))
              ($released (ic:release $state))) $result))
      ($other $other))))
''']
    expected = []

    def add(call, wanted):
        i = len(expected)
        code.append(f"!(println! (LibraryCase {i} {call}))")
        expected.append(["LibraryCase", str(i), wanted])

    examples = [
        "(= (f $x) (+ (* $x 2) 1)) (= (g $y) (+ (* $y 3) 1))",
        "(= (f $x) (+ $x $x)) (= (g $y) (+ $y $y)) (= (h $a $b) (+ $a $b))",
        "(= (f $x) (if (> $x 0) (* $x 2) 0)) (= (g $y) (if (> $y 0) (* $y 3) 0))",
        '(= (f $x) (println! "first")) (= (g $y) (println! "second"))',
        "(= (f $x) (quote (datum $x))) (= (g $y) (quote (datum $y)))",
        "(= (f $x) (superpose ($x (+ $x 2)))) (= (g $y) (superpose ($y (+ $y 3))))",
        "(= (f $x) (if (> $x 0) (+ $x 2) (superpose ()))) (= (g $y) (if (> $y 0) (+ $y 3) (superpose ())))",
        "(= (f $x) (+ (* $x 2) 1)) (= (f $y) (+ (* $y 3) 1))",
    ]
    exported = []
    for text in examples:
        original = reference(text)
        compiled = candidate(original, "ic-learned:test")
        assert compiled
        document, support = compiled
        encoded = lc1("ic-learned:test", document)
        before = len(plain(original))
        cost = ["Saved", str(before), str(len(encoded))] if len(encoded) < before else "NotSmaller"
        add(f"(library-test:factor {text_arg(text)})",
            ["LibraryObservation", str(support), observation(encoded), "true", cost])
        add(f"(== (ic:library-unpack {tensor(encoded)}) (IC:Some (syntax:read {text_arg(text)})))", "true")
        code.append(f"!(println! (LibraryExportCase {len(exported)} (library-test:export {text_arg(text)})))")
        exported.append((text, document))

    for source, count in (
        ('(: f (-> Atom Atom)) (= (f $x) (+ $x 1)) (= (g $x) (+ $x 2))', 1),
        ('(= (f $x) (let $local 1 (+ $x $local))) (= (g $x) (eval $x))', 0),
        ('(= (if $a $b $c) $b) (= (ordinary $x) (+ $x 1))', 1),
    ):
        add(f'''(case (ic:prepare-library (syntax:read {text_arg(source)}))
          (((Library:Prepared $doc $archive $sites) (size-atom $sites)) ($other $other)))''', str(count))
    for head, expected_intrinsic in (("+", "true"), ("if", "true"), ("let", "true"),
                                     ("source-library-custom", "false"), ("42", "false")):
        add(f"(syntax:intrinsic-head? {head})", expected_intrinsic)
    for name in ("plain", "ic-learned:two names", "ic-learned:x) (injected", "ic-learned:"):
        # A nonempty suffix is not required; the other names test reader
        # round-trip admission rather than treating source text as authority.
        want = "true" if name == "ic-learned:" else "false"
        add(f"(source-library:name? {text_arg(name)})", want)

    # Complete packed cost, not the intermediate template archive, decides
    # whether a learned helper is an improvement. This generated family is
    # one training partition, not evidence of held-out programme generalization.
    long_forms = []
    for coefficient in range(2, 6):
        body = "$x"
        for _ in range(4):
            body = f"(+ (* {body} {coefficient}) {coefficient})"
        long_forms.append(f"(= (f{coefficient} $x) {body})")
    long_source = '\n'.join(long_forms)
    long_doc = reference(long_source)
    candidates = [candidate(long_doc, "ic-learned:test", i, j) for i, j in itertools.combinations(range(4), 2)]
    payloads = [lc1("ic-learned:test", row[0]) for row in candidates if row]
    assert len(payloads) == 6 and len(set(payloads)) == 1
    optimum = payloads[0]
    assert len(optimum) < len(plain(long_doc))
    want_search = ["LibrarySearch", observation(optimum), "6", "1", "0", "RegionExhausted"]
    add(f"(library-test:search {text_arg(long_source)} False)", want_search)
    add(f"(library-test:search {text_arg(long_source)} True)", want_search)
    add(f"(library-test:lifecycle {text_arg(long_source)})",
        ["LibraryLifecycle", "BudgetExhausted", "Cancelled", observation(optimum),
         "6", "1", "0", "RegionExhausted"])
    add(f"(ic:learn-library (syntax:read {text_arg(long_source)}) {text_arg('test')} 0)",
        ["IC:SearchRefused", "InvalidQuantum"])
    add(f"(library-test:changed-source {text_arg(long_source)})",
        ["IC:ResumeRejected", "ChangedDependencies"])

    for rejected in (
        "(= (f $x) (+ $x 2)) (= (g $y) (* $y 3))",
        "(= (f $x) (+ $x (+ 1 2))) (= (g $y) (+ $y 3))",
    ):
        assert candidate(reference(rejected), "ic-learned:test") is None
        add(f'''(case (source-library:seed (ic:prepare-library (syntax:read {text_arg(rejected)}))
            "ic-learned:test" 0 1) (((Library:Refused $why) Refused) ($other Unexpected)))''', "Refused")

    base_doc = reference(examples[0])
    good_doc, _support = candidate(base_doc, "ic-learned:test")
    damaged = deepcopy(good_doc)
    # A computation is not an already-established parameter/literal image.
    damaged[3][1][2][1][2][1][1] = reference("(+ 1 2)")[3][0][2]
    add(f"(ic:library-unpack {tensor(lc1('ic-learned:test', damaged))})", "IC:None")
    damaged = deepcopy(good_doc)
    damaged[3][1][2][1][2][1][1] = ["Syntax:Variable", "99"]
    add(f"(ic:library-unpack {tensor(lc1('ic-learned:test', damaged))})", "IC:None")
    for bad in (lc1("ic-learned:test", good_doc, b"bad"), lc1("ic-learned:test", good_doc) + b"bad"):
        add(f"(ic:library-unpack {tensor(bad)})", "IC:None")
    alias_source = "(= (f $x $y) (+ (* $x 2) $y)) (= (g $a $b) (+ (* $a 3) $b))"
    alias_doc = reference(alias_source)
    alias_changed, _support = candidate(alias_doc, "ic-learned:test")
    alias_changed[3][1][2][1][2][1][1] = ["Syntax:Variable", "1"]
    add(f"(ic:library-check {tensor(plain(alias_doc))} {tensor(plain(alias_doc))} "
        f"{tensor(lc1('ic-learned:test', alias_changed))})", ["IC:PackedRejected", "Reconstruction"])
    collision = examples[0] + " (ic-learned:test)"
    add(f"(ic:learn-library (syntax:read {text_arg(collision)}) {text_arg('test')} 64)",
        ["IC:SearchRefused", "InvalidLibraryName"])
    add(f"(ic:export-library {tensor(plain(base_doc))})", ["Library:Refused", "NoLearnedLibrary"])
    for bad_suffix in ("42", "UnknownSuffix"):
        add(f"(ic:learn-library (syntax:read {text_arg(examples[0])}) {bad_suffix} 64)",
            ["IC:SearchRefused", "InvalidLibraryName"])
    foreign = deepcopy(base_doc)
    foreign[2][1] = q("he")
    add(f"(ic:prepare-library {syntax(foreign)})", ["Library:Refused", "DifferentReader"])
    add(f"(ic-search:observation (Library:Model (Library:Prepared {syntax(foreign)} () ()) "
        f'"ic-learned:test") {tensor(plain(base_doc))})', "IC:None")
    foreign_library = deepcopy(good_doc)
    foreign_library[2][1] = q("he")
    add(f"(ic:library-unpack {tensor(lc1('ic-learned:test', foreign_library))})", "IC:None")
    add(f"(ic:export-library {tensor(lc1('ic-learned:test', foreign_library))})",
        ["Library:Refused", "InvalidArtifact"])

    corpus_profiles = []
    for relative in ("lib/petta/list.metta", "lib/petta/postings.metta", "lib/petta/tensor.metta"):
        corpus_text = (ROOT / relative).read_text()
        corpus_doc = reference(corpus_text)
        selected = len(sites(corpus_doc))
        total = sum(1 for form in corpus_doc[3] if form[2][0] == "Syntax:Expr"
                    and form[2][1] and is_symbol(form[2][1][0], "="))
        add(f'''(case (ic:prepare-library (syntax:read {text_arg(corpus_text)}))
          (((Library:Prepared $document $archive $sites) (size-atom $sites)) ($other $other)))''', str(selected))
        corpus_profiles.append({"source": relative, "source_equations": total,
                                "entered_value_sites": selected, "discovery_run": False})

    program = "\n".join(code) + "\n"
    check_controls(program)
    fixture = args.output / "library.metta"
    fixture.write_text(program)
    run = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                         cwd=ROOT, text=True, capture_output=True, timeout=90)
    (args.output / "library.log").write_text(run.stdout + run.stderr)
    actual = [f for f in parse_forms(run.stdout) if isinstance(f, list) and f and f[0] == "LibraryCase"]
    found = {f[1]: f for f in actual}
    wrong = [{"case": i, "expected": wanted, "actual": found.get(str(i))}
             for i, wanted in enumerate(expected) if found.get(str(i)) != wanted]
    sources = [f for f in parse_forms(run.stdout) if isinstance(f, list) and f and f[0] == "LibraryExportCase"]
    text_cases = {int(f[1]): f[2] for f in sources}
    export_checks, execution_checks = [], []
    queries = [
        [('(f 7)', ["15"]), ('(g 7)', ["22"]), ('(f (superpose (1 2)))', ["3", "5"]),
         ('(f (superpose ()))', [])],
        [('(f (superpose (1 2)))', ["2", "4"]), ('(g 3)', ["6"]), ('(h 2 3)', ["5"])],
        [('(f 2)', ["4"]), ('(g 2)', ["6"]), ('(f -1)', ["0"])],
        [('(f 7)', ["true"]), ('(g 9)', ["true"]), ('(f (superpose ()))', []),
         ('(f (println! "ENTRY-EFFECT"))', ["true"])],
        [('(f (quote (superpose (1 2))))', [["datum", ["superpose", ["1", "2"]]]]),
         ('(g 5)', [["datum", "5"]])],
        [('(f 7)', ["7", "9"]), ('(g 7)', ["7", "10"]),
         ('(f (superpose (1 2)))', ["1", "3", "2", "4"])],
        [('(f -1)', []), ('(g 2)', ["5"]), ('(f 2)', ["4"])],
        [('(f 4)', ["9", "13"]), ('(f (superpose (1 2)))', ["3", "5", "4", "7"])],
    ]
    canonical = lambda value: json.dumps(value, sort_keys=True, ensure_ascii=False)
    for i, (original_source, expected_doc) in enumerate(exported):
        data = text_cases.get(i)
        good = isinstance(data, list) and data[:2] == ["TensorData", "u8"]
        if not good:
            export_checks.append({"case": i, "passed": False})
            continue
        text = bytes(int(row[1]) for row in data[3]).decode()
        good = reference(text) == expected_doc
        export_checks.append({"case": i, "passed": good})
        if not good:
            continue
        appendix = '\n'.join(f'!(println! (LibraryRun {j} {call}))' for j, (call, _wanted) in enumerate(queries[i]))
        executions = []
        for label, source in (("original", original_source), ("learned", text)):
            path = args.output / f"execution-{i}-{label}.metta"
            path.write_text(source + '\n' + appendix + '\n')
            process = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(path.resolve())],
                                     cwd=ROOT, text=True, capture_output=True, timeout=20)
            (args.output / f"execution-{i}-{label}.log").write_text(process.stdout + process.stderr)
            forms = parse_forms(process.stdout)
            observations = [f for f in forms if isinstance(f, list) and f and f[0] == "LibraryRun"]
            # The tested effects print String values; standalone command
            # acknowledgements are boolean atoms, not those quoted strings.
            effects = [f for f in forms if isinstance(f, str) and f.startswith('"')]
            expected_answers = [["LibraryRun", str(j), answer] for j, (_call, wanted) in enumerate(queries[i])
                                for answer in wanted]
            expected_effects = ['"first"', '"second"', '"ENTRY-EFFECT"', '"first"'] if i == 3 else []
            passed = process.returncode == 0 and Counter(map(canonical, observations)) == Counter(map(canonical, expected_answers))
            passed = passed and effects == expected_effects
            executions.append({"variant": label, "passed": passed, "answers": len(observations),
                               "effects": effects, "exit": process.returncode})
        execution_checks.append({"case": i, "passed": all(row['passed'] for row in executions),
                                 "results": executions})
    all_exports = len(sources) == len(exported) and len(export_checks) == len(exported) and all(
        row['passed'] for row in export_checks)
    all_executions = len(execution_checks) == len(exported) and all(row['passed'] for row in execution_checks)
    receipt = {"passed": run.returncode == 0 and len(actual) == len(expected) and not wrong and all_exports and all_executions,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong,
               "export_checks": export_checks, "execution_checks": execution_checks,
               "generated_training_family_bytes": [len(plain(long_doc)), len(optimum)],
               "real_source_intake": corpus_profiles,
               "scope": "entered-value source templates, complete LC1 encoding and independent emitted-library execution"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [row["case"] for row in wrong]}))
    for row in wrong[:3]:
        print(json.dumps(row)[:2200])
    if run.returncode:
        print(run.stderr[-1500:])
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
