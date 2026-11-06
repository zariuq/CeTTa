#!/usr/bin/env python3
"""Generate the original nine Opt application families and their scale ladder."""
import argparse
import hashlib
import json
from pathlib import Path
import re

import workloads as w


# These are the original application sizes, not a replacement microbenchmark.
CASES = {
    "transitive_50k": (w.transitive, (2000, 50000), "&t", {"trans": 2, "dtrans": 3}),
    "clique5_200x3600": (w.clique, (200, 3600, 5), "&t", {"5-clique": 5}),
    "oddeven_300": (w.odd_even_sort, (300,), "&self", {"A": 2}),
    "bc_1000": (w.backward_chain, (1000,), "&p", {"proof": 2}),
    "hexlife_20_80_20": (w.hexlife, (20, 80, 20), "&self", {"alive": 1}),
    "ancestry_1k": (w.ancestry, (1000, 40, 20), "&t", {"child": 2, "generation": 3, "ancestor": 2}),
    "reach_500": (w.reach_paths, (500, 2), "&r", {"reachable": 3, "step": 3, "visited": 1}),
    "counter_300": (w.counter_machine, (300,), "&m", {"state": 2, "HALTED": 1}),
    "pc_200": (w.process_calculus, (1000, 200, 200), "&p", {"petri": 1}),
}
BASE_CASES = tuple(CASES)
CASES.update({
    "transitive_small": (w.transitive, (12, 35), "&t", {"trans": 2, "dtrans": 3}),
    "clique_small": (w.clique, (9, 24, 5), "&t", {"5-clique": 5}),
    "clique_small_early": (w.clique, (9, 24, 5, "early"), "&t", {"5-clique": 5}),
    "clique5_200x3600_early": (w.clique, (200, 3600, 5, "early"), "&t", {"5-clique": 5}),
    "oddeven_small": (w.odd_even_sort, (8,), "&self", {"A": 2}),
    "bc_small": (w.backward_chain, (5,), "&p", {"proof": 2}),
    "hexlife_small": (w.hexlife, (6, 10, 3), "&self", {"alive": 1}),
    "ancestry_small": (w.ancestry, (20, 8, 4), "&t", {"child": 2, "generation": 3, "ancestor": 2}),
    "reach_small": (w.reach_paths, (12, 2), "&r", {"reachable": 3, "step": 3, "visited": 1}),
    "counter_small": (w.counter_machine, (3,), "&m", {"state": 2, "HALTED": 1}),
    "pc_small": (w.process_calculus, (30, 4, 4), "&p", {"petri": 1}),
    "transitive_200k": (w.transitive, (8000, 200000), "&t", {"trans": 2, "dtrans": 3}),
    "transitive_1m": (w.transitive, (50000, 1000000), "&t", {"trans": 2, "dtrans": 3}),
    "transitive_5m": (w.transitive, (250000, 5000000), "&t", {"trans": 2, "dtrans": 3}),
    "oddeven_2000": (w.odd_even_sort, (2000,), "&self", {"A": 2}),
    "oddeven_5000": (w.odd_even_sort, (5000,), "&self", {"A": 2}),
    "clique5_300x8100": (w.clique, (300, 8100, 5), "&t", {"5-clique": 5}),
    "bc_3000": (w.backward_chain, (3000,), "&p", {"proof": 2}),
    "hexlife_28_200_30": (w.hexlife, (28, 200, 30), "&self", {"alive": 1}),
    "ancestry_3k": (w.ancestry, (3000, 40, 20), "&t", {"child": 2, "generation": 3, "ancestor": 2}),
    "reach_1500": (w.reach_paths, (1500, 2), "&r", {"reachable": 3, "step": 3, "visited": 1}),
    "counter_1000": (w.counter_machine, (1000,), "&m", {"state": 2, "HALTED": 1}),
    "pc_500": (w.process_calculus, (2500, 500, 500), "&p", {"petri": 1}),
})

SET_HELPER = """!(import_prolog_function variant_sha1)
!(bind! &set-keys (new-space))
(= (add-set-atom $space $atom)
   (let $key (seen $space (variant_sha1 $atom))
     (if (== () (collapse (once (match &set-keys $key True))))
         (let $_ (add-atom &set-keys $key) (add-atom $space $atom))
         True)))
"""


def swi_source(source):
    spaces = re.findall(r"\(bind! (&\S+) \(new-space set\)\)", source)
    if not spaces:
        return source
    for space in spaces:
        source = source.replace(f"(add-atom {space} ", f"(add-set-atom {space} ")
    # Install the ordinary SWI adapter before the first set-space declaration.
    at = source.index("!(bind!")
    return source[:at] + SET_HELPER + source[at:]


def generate(directory, names, observe=False):
    directory.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name in names:
        make, arguments, space, heads = CASES[name]
        values = make(*arguments)
        mm2, metta = values[:2]
        bag = values[3] if len(values) == 4 else metta
        # Use the original direct bag plan where it already yields distinct
        # rows; recursive relations with repeated proofs need alpha-set insert.
        swi = bag if make in (w.transitive, w.clique, w.odd_even_sort,
                              w.counter_machine, w.hexlife) else swi_source(metta)
        if observe:
            # The bag plans store their results in &self, not in a set space.
            swi_space = space if space == "&self" or f"(bind! {space} " in swi else "&self"
            for head, arity in heads.items():
                variables = " ".join(f"$v{i}" for i in range(arity))
                pattern = f"({head} {variables})"
                # The MeTTa process representation renames punctuation heads.
                template = "$v0" if head == "petri" else pattern
                query = pattern if head != "petri" else "$v0"
                metta += f"!(let $row (match {space} {query} {template}) (println! (OBSERVED $row)))\n"
                swi += f"!(let $row (match {swi_space} {query} {template}) (println! (OBSERVED $row)))\n"
        files = {}
        for suffix, content in (("mm2", mm2), ("metta", metta), ("swi.metta", swi)):
            path = directory / f"{name}.{suffix}"
            path.write_text(content)
            files[suffix] = dict(path=path.name, sha256=hashlib.sha256(content.encode()).hexdigest())
        manifest.append(dict(case=name, family=make.__name__, arguments=arguments,
                             heads=heads, files=files, observer="complete projected set" if observe else "relation cardinalities"))
        print(name, flush=True)
    (directory / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--case", action="append", choices=tuple(CASES))
    parser.add_argument("--observe", action="store_true", help="publish complete result rows for small semantic controls")
    args = parser.parse_args()
    generate(args.output, args.case or BASE_CASES, args.observe)
