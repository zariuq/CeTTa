#!/usr/bin/env python3
"""Generate typed scene examples from an assembled module, then check execution.

Constructor signatures and notation are read from CeTTa's assembled LanguageDef.
The independent product-model evaluator is the reference, not a second grammar.
"""

from __future__ import annotations

import argparse
from functools import lru_cache
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from gen_semiring_fixtures import parse_forms, sexpr


def canonical_list(value):
    result = []
    while isinstance(value, list) and len(value) == 3 and value[0] == "LCons":
        result.append(value[1])
        value = value[2]
    if value != "LNil":
        raise ValueError("not a canonical list")
    return result


def quoted(value):
    return json.loads(value)


def model(term):
    head, *args = term
    if head in {"Circle", "Square", "Black", "White"}:
        return head
    if head in {"Object", "Pair"}:
        return tuple(model(arg) for arg in args)
    if head in {"ShapeOf", "LeftOf"}:
        return model(args[0])[0]
    if head in {"ColourOf", "RightOf"}:
        return model(args[0])[1]
    return {
        "BlackCircle": ("Circle", "Black"), "WhiteCircle": ("Circle", "White"),
        "WhiteSquare": ("Square", "White"), "BlackSquare": ("Square", "Black"),
    }[head]


def reify(value, category):
    if category in {"Shape", "Colour"}:
        return [value]
    if category == "Object":
        return ["Object", [value[0]], [value[1]]]
    return ["Pair", reify(value[0], "Object"), reify(value[1], "Object")]


def setup(module):
    return f'''(($reader (langdef:load-baked-parser "module-core-reader"))
      ($source (module:read-file $reader "feature-scene" {json.dumps(module)}))
      ($reader-done (langdef:close-parser $reader))
      ($presentation (module:assemble $source "Glyphs" LNil LNil OriginViewV1 "feature-scene"))
      ($core (module:core $presentation "FeatureScene")))'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--module", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--per-constructor", type=int, default=8)
    args = ap.parse_args()
    if args.per_constructor < 1:
        ap.error("--per-constructor must be positive")
    args.output.mkdir(parents=True, exist_ok=True)
    binary = args.cetta.resolve()

    def run(name, text):
        path = args.output / f"{name}.metta"
        path.write_text(text)
        result = subprocess.run([str(binary), "--lang", "petta", "--profile", "extended", str(path.resolve())],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        (args.output / f"{name}.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(result.stderr)
        return parse_forms(result.stdout)

    module_path = os.path.relpath(args.module.resolve(), ROOT)
    prefix = '!(import! &self (library lib_module))\n'
    forms = run("core", prefix + f"!(let* {setup(module_path)} (println! (SceneCore $core)))\n")
    cores = [form[1] for form in forms if isinstance(form, list) and form and form[0] == "SceneCore"]
    if len(cores) != 1 or cores[0][0] != "GSLTLanguageDefWireV1":
        raise ValueError("module did not assemble one core")
    core = cores[0]
    rows = canonical_list(core[3])
    constructors = {}
    for row in rows:
        if row[0] != "GrammarRule":
            raise ValueError("unsupported grammar descriptor")
        label, category = quoted(row[1]), quoted(row[2])
        parameters = []
        for parameter in canonical_list(row[3]):
            if parameter[0] != "TermSimple" or parameter[2][0] != "TBase":
                raise ValueError("this generator accepts first-order base-sort parameters")
            parameters.append((quoted(parameter[1]), quoted(parameter[2][1])))
        constructors[label] = category, parameters, canonical_list(row[4])

    def render(term):
        category, parameters, syntax = constructors[term[0]]
        arguments = {name: render(arg) for (name, _), arg in zip(parameters, term[1:], strict=True)}
        pieces = []
        for item in syntax:
            if item[0] == "SyntaxTerminal":
                pieces.append(quoted(item[1]))
            elif item[0] == "SyntaxNonTerminal":
                pieces.append(arguments[quoted(item[1])])
            else:
                raise ValueError("unsupported notation item")
        return " ".join(pieces)

    @lru_cache(None)
    def generate(category, depth):
        terms = []
        for name, (result, parameters, _) in constructors.items():
            if result != category:
                continue
            if not parameters:
                terms.append((name,))
            elif depth:
                pools = [generate(sort, depth - 1) for _, sort in parameters]
                terms.extend((name, *children) for children in
                             itertools.islice(itertools.product(*pools), args.per_constructor))
        return tuple(terms)

    examples = []
    for category in ("Shape", "Colour", "Object", "Scene"):
        train = set(generate(category, 2))
        for term in generate(category, 3):
            examples.append({"sort": category, "term": term, "source": render(term),
                             "normal": reify(model(term), category),
                             "split": "train" if term in train else "heldout-depth"})
    (args.output / "examples.json").write_text(json.dumps(examples, ensure_ascii=False, indent=2) + "\n")
    (args.output / "constructor-profiles.json").write_text(json.dumps(constructors, ensure_ascii=False, indent=2) + "\n")
    normalizer = '''
(= (scene:normal $authority $category $fuel $term)
   (if (== $fuel 0) (Scene:Incomplete $term)
     (let $step (langdef:rewrite-term-at-data $authority $category $category 16 100000
                 (LangDef:RelationRows) $term)
       (if (and (== (car-atom $step) LangDef:ContextualTermResults)
                (== (index-atom $step 1) LangDef:ContextComplete))
         (if (== (size-atom $step) 2) (Scene:Normal $term)
           (case (index-atom $step 2)
             (((LangDef:ContextualTermResult $next $trace)
               (scene:normal $authority $category (- $fuel 1) $next))
              ($other (Scene:Fault $other)))))
         (Scene:Fault $step)))))
'''
    bindings = setup(module_path)[1:-1] + '\n($authority (langdef:load-language-value $core))\n'
    for category in ("Shape", "Colour", "Object", "Scene"):
        bindings += f'''($language{category} (module:language $presentation "FeatureScene" "{category}" (SyntaxLexicalPolicyV1 bnfc-documented)))
($syntax{category} (module:language-syntax $language{category}))
($parser{category} (langdef:compile-parser $syntax{category} auto))\n'''
    for index, example in enumerate(examples):
        category = example["sort"]
        bindings += f'''($parse{index} (langdef:parser-parse-text $parser{category} {json.dumps(example['source'], ensure_ascii=False)}))
($project{index} (langdef:project-parser-result $authority "{category}" $parse{index}))
($term{index} (langdef:projected-term $project{index} 0))
($normal{index} (scene:normal $authority "{category}" 64 $term{index}))
($out{index} (println! (SceneCase {index} $term{index} $normal{index})))\n'''
    for category in ("Shape", "Colour", "Object", "Scene"):
        bindings += f'($close{category} (langdef:close-parser $parser{category}))\n'
    bindings += '($closeAuthority (langdef:close-language $authority))\n'
    forms = run("generated", prefix + normalizer + f"!(let* ({bindings}) (println! SceneComplete))\n")
    actual = [form for form in forms if isinstance(form, list) and form and form[0] == "SceneCase"]

    def lists(value):
        return [lists(x) for x in value] if isinstance(value, (list, tuple)) else value

    expected = [["SceneCase", str(i), lists(e["term"]), ["Scene:Normal", e["normal"]]] for i, e in enumerate(examples)]
    wrong = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    receipt = {"passed": len(actual) == len(expected) and not wrong and forms.count("SceneComplete") == 1,
               "examples": len(examples), "heldout_depth": sum(e["split"] == "heldout-depth" for e in examples),
               "observed": len(actual), "mismatches": wrong,
               "scope": "module-driven generation and finite semantic correspondence; not learned inference rules"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not receipt["passed"]:
        for index in wrong[:3]:
            print("expected", sexpr(expected[index]))
            print("actual  ", sexpr(actual[index]))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
