#!/usr/bin/env python3
"""Generate the nuPLN learning benchmark from Nil Geisweiller's nupln.metta.

nuPLN (experimental/nupln/nupln.metta in the trueagi-io PLN repository)
learns a predicate from three labelled examples by backward chaining: it
constructs the conjunction F and G together with the typing and evaluation
proofs that explain every label. The benchmark keeps the eight inference
equations and the knowledge base unchanged, drops the random-sampling helper
(it consults a Prolog file and is not used by the learning query) and the
self-tests, and repeats the file's final learning query N times as top-level
queries, so parsing, loading and printing every answer stay inside the
measured process. The same file runs on SWI-PeTTa and on CeTTa's PeTTa lane.

With --distractors K, K always-true predicates D1 ... DK over the same three
inputs are added to the knowledge base before F and G. Depth-first search
then types every conjunction built on a distractor before it reaches F; none
of them fits the proof-size budget, so the answer is unchanged.

usage: generate.py NUPLN_METTA OUTPUT.metta [--repeat 1000] [--distractors K]
"""
import argparse
import hashlib
from pathlib import Path

QUERY = ("!(once (nupln_bc &kb 27 (: $prf (, (≞ (→ (⊙ 0) $pred) ⊤)\n"
         "                              (, (≞ (→ (⊙ 1) $pred) ⊥)\n"
         "                                 (≞ (→ (⊙ 2) $pred) ⊥))))))\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--repeat", type=int, default=1000)
    parser.add_argument("--distractors", type=int, default=0)
    args = parser.parse_args()
    lines = args.source.read_text(encoding="utf-8").splitlines(keepends=True)
    start = next(i for i, line in enumerate(lines) if line.startswith(";; Combinators"))
    # Back up to the banner line above the section title.
    while start > 0 and lines[start - 1].startswith(";;;"):
        start -= 1
    last_kb = max(i for i, line in enumerate(lines) if line.startswith("!(add-atom &kb "))
    body = "".join(lines[start:last_kb + 1])
    if "nupln_uniform" in body or "import_prolog" in body or "!(test" in body:
        raise ValueError("unexpected nupln.metta layout: the helper or the tests are inside the kept part")
    if body.count("(= (nupln_bc ") != 8:
        raise ValueError("expected the eight nupln_bc inference equations")
    if args.distractors:
        anchor = "!(bind! &kb (new-space))\n"
        if body.count(anchor) != 1:
            raise ValueError("expected one knowledge-base declaration")
        facts = "".join(f"!(add-atom &kb (: D{k} (-> UInt Bool)))\n"
                        + "".join(f"!(add-atom &kb (: d{k}x{x}t (≞ (→ (⊙ {x}) D{k}) ⊤)))\n" for x in range(3))
                        for k in range(1, args.distractors + 1))
        body = body.replace(anchor, anchor + facts)
    digest = hashlib.sha256(args.source.read_bytes()).hexdigest()
    header = (f";; nuPLN learning benchmark generated from nupln.metta (sha256 {digest}):\n"
              ";; the inference equations and knowledge base unchanged, the final\n"
              f";; learning query repeated {args.repeat} times, {args.distractors} distractors.\n\n")
    args.output.write_text(header + body + "\n" + QUERY * args.repeat, encoding="utf-8")
    print(f"{args.output}: {args.repeat} queries, source sha256 {digest}")


if __name__ == "__main__":
    main()
