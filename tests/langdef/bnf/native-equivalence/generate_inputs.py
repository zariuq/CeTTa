#!/usr/bin/env python3
"""Deterministic input documents for the EBNF projection equivalence gate.

Reproduces the input-generation logic of the recorded external-parser
benchmark (runtime/external-parser-bench-20260914/run.py) so the gate runs
from a fresh checkout without that workstation's runtime evidence directory.
Grammar files are committed fixtures under ./grammars (bytes inherited from
the same benchmark's prepared grammars)."""

import pathlib
import sys


def main():
    out = pathlib.Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    documents = {
        'arithmetic_small.input': '1+2*3',
        'duplicate.input': 'xx',
        'nullable.input': '',
        'ambiguous.input': 'x',
        'left_recursive.input': 'xxx',
        'json_small.input': '{"x":1,"x":2,"s":"a\\n"}',
        'arithmetic_5000.input': '+'.join(['1*2'] * 1250),
        'json_5000.input': '[' + ','.join(['{"x":1,"x":2,"s":"a\\n"}'] * 200) + ']',
        'csv_5000.input': '\n'.join(['"a,b",2,"q""x"'] * 333),
        'arithmetic_reachable_5000.input': '+'.join(['1*2'] * 1250),
        'json_reachable_5000.input': '[' + ','.join(['{"x":1,"x":2,"s":"a\\n"}'] * 200) + ']',
        'control_target.input': 'ab',
        'arithmetic_bad.input': '1+*2',
        'json_bad.input': '[1,]',
        'arithmetic_17000.input': '+'.join(['1*2'] * 4250),
        'json_17000.input': '[' + ','.join(['{"x":1,"x":2,"s":"a\\n"}'] * 680) + ']',
    }
    for name, text in documents.items():
        (out / name).write_text(text)
    print(f'generated {len(documents)} input documents in {out}')


main()
