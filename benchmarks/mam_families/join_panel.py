#!/usr/bin/env python3
"""Generate cyclic and acyclic MM2/HE/PeTTa joins with occurrence semantics.

    Complete count and complete witness delivery are separate observations.
    The count sink is one native MM2 plan, not a claim of the best MORK plan.
    Engine loading and publication boundaries are supplied by the paired runner.
"""
import argparse
import hashlib
import json
from pathlib import Path

from joins import (Factor, occurrence_join, metta_count, metta_witnesses,
                   mm2_count, mm2_witnesses)


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def emit(out, name, factors, observer, expected, mork_factors=None):
    counting = observer == "count"
    count = expected if counting else len(expected)
    atoms = [f"(join-count {count})"] if counting else [
        "(join-witness " + " ".join(map(str, identities)) + ")" for identities in expected]
    text = "".join(atom + "\n" for atom in atoms)
    (out / f"{name}.expected").write_text(text)
    sources = {
        "he": metta_count("he", factors) if counting else metta_witnesses(factors),
        "petta": metta_count("petta", factors) if counting else metta_witnesses(factors),
        "mork": mm2_count(mork_factors or factors) if counting else mm2_witnesses(factors),
    }
    dialects = {}
    for language, source in sources.items():
        directory = out / language
        directory.mkdir(exist_ok=True)
        path = directory / f"{name}.{'mm2' if language == 'mork' else 'metta'}"
        path.write_text(source)
        entry = {"source": str(path.relative_to(out)), "sha256": digest(source)}
        if language != "mork":
            entry["profile"] = "extended"
            if counting:
                scalar = str(count) + "\n"
                scalar_path = directory / f"{name}.expected"
                scalar_path.write_text(scalar)
                entry.update(expected=str(scalar_path.relative_to(out)),
                             expected_sha256=digest(scalar))
        dialects[language] = entry
    return {
        "case": name, "family": "join", "role": observer,
        "params": {"factors": len(factors), "rows": sum(len(f.rows) for f in factors),
                   "relation_sizes": [len(f.rows) for f in factors],
                   "projection_heads": ["join-count" if counting else "join-witness"]},
        "answers": len(atoms), "expected": f"{name}.expected", "expected_sha256": digest(text),
        "contract": "occurrence-bag", "dialects": dialects,
        "oracle": "independent source-ordered indexed binary row-occurrence enumeration",
        "semantics": {
            "problem": "finite ground relational conjunction", "observer": observer,
            "order": "unobserved", "multiplicity": "one contribution per physical row tuple",
            "read_window": "frozen input facts", "control": "one query, pure constant or witness template",
            "comparison": "idiomatic solutions to the same problem",
            "presentation": "MeTTa scalar; MM2 tagged count" if counting else "row-identity tuples",
            "metta_plan": "collection cardinality" if counting else "full witness join",
            "mm2_plan": ("covering-factor-first count sink" if mork_factors else "full-query count sink")
                if counting else "full witness join",
            "mm2_factor_order": [factor.head for factor in (mork_factors or factors)],
            "publication": "CeTTa result stream; native MORK final store with result-head projection",
            "empty_count": "MM2's absent empty group is observed through an empty witness set",
            "private_fields": "distinct occurrence IDs preserve duplicates across all stores",
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

    def add(name, factors, formula=None, witnesses=False, mork_factors=None):
        count = sum(1 for _ in occurrence_join(factors))
        if formula is not None:
            assert count == formula, (name, count, formula)
        if count:
            manifest.append(emit(out, name + "_count", factors, "count", count, mork_factors))
        if witnesses or not count:
            manifest.append(emit(out, name + "_witnesses", factors, "witnesses",
                                 list(occurrence_join(factors))))

    for width in ((4, 8) if args.scale == "smoke" else (8, 16, 32, 64)):
        pairs = tuple((x, y) for x in range(width) for y in range(width))
        triangle = (Factor("r", ("x", "y"), pairs),
                    Factor("s", ("y", "z"), pairs),
                    Factor("t", ("z", "x"), pairs))
        add(f"triangle_dense_{width}", triangle, width ** 3, width == 4)
    for width in ((4, 8) if args.scale == "smoke" else (8, 16, 32, 64)):
        pairs = tuple((x, y) for x in range(width) for y in range(width))
        guard = Factor("u", ("x", "y", "z"),
                       tuple((x, y, (x + y) % width) for x, y in pairs))
        factors = (Factor("r", ("x", "y"), pairs),
                   Factor("s", ("y", "z"), pairs),
                   Factor("t", ("z", "x"), pairs), guard)
        add(f"guarded_triangle_{width}", factors, width ** 2,
            mork_factors=(guard, *factors[:3]))
    for size in ((8, 16) if args.scale == "smoke" else (64, 256, 1024)):
        skew = (Factor("r", ("x", "y"), tuple((x, 0) for x in range(size))),
                Factor("s", ("y", "z"), tuple((0, z) for z in range(size))),
                Factor("t", ("z", "x"), tuple((z, z) for z in range(size))))
        add(f"triangle_skew_{size}", skew, size, args.scale == "smoke")
    rows = tuple((x, y) for x in range(2) for y in range(2)) * 2
    duplicates = (Factor("r", ("x", "y"), rows),
                  Factor("s", ("y", "z"), rows),
                  Factor("t", ("z", "x"), rows))
    add("triangle_duplicates", duplicates, 64, True)
    width = 4 if args.scale == "smoke" else 16
    star = tuple(Factor(head, ("x", variable), tuple((0, y) for y in range(width)))
                 for head, variable in zip(("r", "s", "t"), ("y", "z", "w")))
    add(f"acyclic_star_{width}", star, width ** 3, args.scale == "smoke")
    width = 4 if args.scale == "smoke" else 8
    pairs = tuple((x, y) for x in range(width) for y in range(width))
    cycle = tuple(Factor(head, variables, pairs) for head, variables in
                  zip(("r", "s", "t", "u"), (("x", "y"), ("y", "z"), ("z", "w"), ("w", "x"))))
    add(f"cycle_four_{width}", cycle, width ** 4, args.scale == "smoke")
    aliases = (Factor("r", ("x", "x"), ((0, 0), (0, 1), (1, 1))),
               Factor("s", ("x", "z"), ((0, 0), (1, 0))),
               Factor("t", ("z", "x"), ((0, 0), (0, 1))))
    add("join_private_alias", aliases, 2, True)
    empty = (Factor("r", ("x", "y"), ((0, 0),)),
             Factor("s", ("y", "z"), ((0, 0),)), Factor("t", ("z", "x"), ()))
    add("triangle_empty", empty, 0, True)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Generated {len(manifest)} native join cases with independent occurrence oracles")


if __name__ == "__main__":
    main()
