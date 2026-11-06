#!/usr/bin/env python3
"""Reproduce Opt's large two-hop/triangle materialization inputs (NumPy PCG64)."""
import argparse
import json
from pathlib import Path


def generate(output, name, nodes, draws):
    import numpy as np
    if nodes < 1 or draws < 0:
        raise ValueError("positive node count and nonnegative edge draws required")
    output.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(0)
    keys = np.unique(rng.integers(0, nodes, draws, dtype=np.int64) * nodes
                     + rng.integers(0, nodes, draws, dtype=np.int64))
    mm2_rules = ("(exec 0 (, (edge $x $y) (edge $y $z)) (, (trans $x $z)))\n"
                 "(exec 1 (, (edge $x $y) (edge $y $z) (edge $x $z)) (, (dtrans $x $y $z)))\n")
    metta_rules = (
        "; the derived facts live in a set space, as MM2's do; each rule runs for its effect\n"
        "!(bind! &t (new-space set))\n"
        "(= (rule-trans) (match &self (, (edge $x $y) (edge $y $z)) (add-atom &t (trans $x $z))))\n"
        "(= (rule-dtrans) (match &self (, (edge $x $y) (edge $y $z) (edge $x $z)) (add-atom &t (dtrans $x $y $z))))\n"
        "!(let $r (collapse (rule-trans)) done)\n"
        "!(let $r (collapse (rule-dtrans)) done)\n"
        "!(size-atom (collapse (match &t (trans $x $z) t)))\n"
        "!(size-atom (collapse (match &t (dtrans $x $y $z) t)))\n")
    with (output / f"{name}.mm2").open("w") as mm2, (output / f"{name}.metta").open("w") as metta:
        for start in range(0, len(keys), 1 << 20):
            chunk = keys[start:start + (1 << 20)]
            text = "".join(f"(edge {a} {b})\n" for a, b in
                           zip((chunk // nodes).tolist(), (chunk % nodes).tolist()))
            mm2.write(text)
            metta.write(text)
        mm2.write(mm2_rules)
        metta.write(metta_rules)
    description = dict(name=name, nodes=nodes, draws=draws, distinct_edges=len(keys),
                       seed=0, numpy=np.__version__, generator="numpy.random.default_rng/PCG64",
                       task="distinct two-hop endpoint pairs and closed directed triples; not fixed-point closure")
    (output / f"{name}.json").write_text(json.dumps(description, indent=2) + "\n")
    print(json.dumps(description))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("name")
    parser.add_argument("nodes", type=int)
    parser.add_argument("draws", type=int)
    args = parser.parse_args()
    generate(args.output, args.name, args.nodes, args.draws)
