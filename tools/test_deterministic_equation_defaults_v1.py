#!/usr/bin/env python3
"""Ordinary authored equations are unbounded; explicit work remains bounded."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    args = parser.parse_args()
    directory = ROOT / 'runtime/equation-default-tests'
    directory.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='controls-', dir=directory))
    text = 'x' * 100000
    source = f'''
!(import! &self langdef)
!(import! &self str)
!(import! &self system)
!(let* (($program (langdef:load-deterministic-equations
           (LangDef:EquationSources (str:concat (system:runtime-root)
             "/langdef/bnf/plain_bnf_denotation_v1.metta"))))
        ($value (langdef:run-deterministic-equations-data $program
           (LangDef:DeterministicCall bnf-v1:string->text {json.dumps(text)})))
        ($back (langdef:run-deterministic-equations-data $program
           (LangDef:DeterministicCall bnf-v1:text->string $value)))
        ($ordinary (langdef:run-deterministic-equations $program
           (LangDef:DeterministicCall bnf-v1:text->string
             (bnf-v1:string->text "ordinary"))))
        ($bounded (langdef:run-deterministic-equations-allowance $program
           (LangDef:DeterministicCall bnf-v1:string->text "bounded") 1)))
   (println! (EquationDefaults
     (== $back {json.dumps(text)})
     (== $ordinary "ordinary")
     (case $bounded (((Error $operation $reason) true) ($other false))))))
'''
    path = output / 'controls.metta'
    path.write_text(source)
    ran = subprocess.run([str(args.cetta.resolve()), '--no-results', '--lang',
                          'petta', str(path)], cwd=ROOT, capture_output=True, text=True)
    assert ran.returncode == 0, ran.stderr or ran.stdout[:2000]
    assert ran.stdout.strip() == '(EquationDefaults true true true)', ran.stdout[:2000]
    print('DeterministicEquationDefaults 3 0')


if __name__ == '__main__':
    main()
