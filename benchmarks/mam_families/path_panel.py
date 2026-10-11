#!/usr/bin/env python3
"""Generate native MM2, HE and PeTTa path queries with declared observations.

Each complete fact has a row identity. Equal payloads therefore remain separate
occurrences on all engines, including MORK's set store. Count cases include
numeric folds, collection cardinality and MM2's count sinks and separator plans.
Witness cases compare full joins.
Loading and native output publication belong to the paired runner's boundary.
MORK exports its whole final store; the declared result-head readout is separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random

from paths import (Row, enumerate_witnesses, metta_count, metta_witnesses,
                   mm2_count, mm2_independent_count, mm2_witnesses)


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def emit(out, name, relations, observation, expected, oracle, independent=False,
         embedded=False, cardinality=False):
    levels = len(relations)
    sources = {
        "he": metta_count("he", relations, embedded, cardinality) if observation == "count" else metta_witnesses(relations),
        "petta": metta_count("petta", relations, embedded, cardinality) if observation == "count" else metta_witnesses(relations),
        "mork": mm2_independent_count(relations) if independent else
            mm2_count(relations) if observation == "count" else mm2_witnesses(relations),
    }
    expected_text = "".join(atom + "\n" for atom in expected)
    (out / f"{name}.expected").write_text(expected_text)
    dialects = {}
    for language, source in sources.items():
        directory = out / language
        directory.mkdir(exist_ok=True)
        path = directory / f"{name}.{'mm2' if language == 'mork' else 'metta'}"
        path.write_text(source)
        dialects[language] = {"source": str(path.relative_to(out)),
                              "sha256": digest(source)}
        if language != "mork":
            dialects[language]["profile"] = "extended"
            if observation == "count" and not embedded:
                # Scalar versus tagged scalar is an explicit presentation
                # adapter, with the same mathematical count observation.
                scalar = expected[0].removeprefix("(path-count ").removesuffix(")") + "\n"
                oracle_path = directory / f"{name}.expected"
                oracle_path.write_text(scalar)
                dialects[language].update(expected=str(oracle_path.relative_to(out)),
                                         expected_sha256=digest(scalar))
        elif independent:
            dialects[language]["steps"] = 2
    return {
        "case": name, "family": "path", "role": observation,
        "params": {"levels": levels, "rows": sum(map(len, relations)),
                   "relation_sizes": list(map(len, relations)),
                   "projection_heads": ["path-count" if observation == "count" else "path-witness"]},
        "answers": len(expected), "expected": f"{name}.expected",
        "expected_sha256": digest(expected_text), "contract": "occurrence-bag",
        "dialects": dialects, "oracle": oracle,
        "semantics": {
            "problem": "full finite ground path join",
            "observer": "occurrence count" if observation == "count" else "row-identity witness bag",
            "presentation": ("row-identity tuples" if observation != "count" else
                "tagged count inside a MeTTa constructor" if embedded else
                "MeTTa scalar count; MM2 tagged count, both interpreted as the same integer"),
            "metta_plan": "collection cardinality" if cardinality else
                "numeric occurrence fold" if observation == "count" else "full witness join",
            "comparison": "idiomatic solutions to the same problem",
            "order": "unobserved", "multiplicity": "one contribution per row-occurrence tuple",
            "read_window": "frozen input facts", "control": "one query, no recursive or effectful templates",
            "publication": "CeTTa result stream; native MORK final store with result-head projection",
            "empty_count": "MM2's missing empty group is not a scalar zero; empty joins use witness observation",
            "mm2_plan": "separate relation counts and scalar product" if independent else "native full-query count sink or witness join",
            "admission": "fixed separator 0; nonempty relations; result below 2^63" if independent else "finite ground rows with occurrence identities",
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--scale", choices=("smoke", "measure"), default="measure")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest = []

    def add(name, relations, count=None):
        if count is None:
            count = len(enumerate_witnesses(relations))
            oracle = "independent nested row-occurrence enumeration"
        else:
            oracle = "independent product/isolated-key formula; smoke scale also enumerated"
            if len(relations) <= 8:
                assert count == len(enumerate_witnesses(relations))
        if count:
            manifest.append(emit(out, name + "_count", relations, "count",
                                 [f"(path-count {count})"], oracle))
        if len(relations) <= 8 or count == 0:
            witnesses = enumerate_witnesses(relations)
            assert len(witnesses) == count
            atoms = ["(path-witness " + " ".join(map(str, witness)) + ")"
                     for witness in witnesses]
            manifest.append(emit(out, name + "_witnesses", relations, "witnesses", atoms,
                                 "independent nested row-occurrence enumeration"))

    levels = (4, 8) if args.scale == "smoke" else (4, 8, 12, 16)
    for size in levels:
        product = [[Row(0, 0, -3), Row(0, 0, 5)] for _ in range(size)]
        add(f"path_product_{size}", product, 2 ** size)
        manifest.append(emit(out, f"path_product_{size}_separator_count", product,
                             "count", [f"(path-count {2 ** size})"],
                             "independent product formula; separator factorization", independent=True))
        manifest.append(emit(out, f"path_product_{size}_cardinality_count", product,
                             "count", [f"(path-count {2 ** size})"],
                             "independent product formula; idiomatic cardinality and MM2 separator plan",
                             independent=True, cardinality=True))
        skew = [[Row(0, 0, 1), Row(0, 0, 1), Row(1, 1, 9)] for _ in range(size)]
        add(f"path_skew_{size}", skew, 2 ** size + 1)
    size = levels[-1]
    product = [[Row(0, 0, -3), Row(0, 0, 5)] for _ in range(size)]
    manifest.append(emit(out, f"path_product_{size}_embedded_count", product,
                         "count", [f"(path-count {2 ** size})"],
                         "independent product formula; embedded consumer control", embedded=True))
    rng = random.Random(8917)
    random_rows = [[Row(rng.randrange(3), rng.randrange(3), rng.randrange(-7, 8))
                    for _ in range(5)] for _ in range(6)]
    for relation in random_rows:
        relation.append(relation[0])
    add("path_correlated_6", random_rows)
    width = 8 if args.scale == "smoke" else 512
    isolated = [[Row(i, i, i) for i in range(width)] for _ in range(8)]
    add(f"path_isolated_8_{width}", isolated, width)
    empty = [[Row(0, 0, 1)] for _ in range(5)]
    empty[2] = []
    add("path_empty_5", empty)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Generated {len(manifest)} path cases for native MM2, HE and PeTTa")


if __name__ == "__main__":
    main()
