#!/usr/bin/env python3
"""Check dynamic calls against independently specified dialect observations."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from petta_corpus_manifest import alpha_canonicalize_atom_text

CASES = [{'name': 'one',
  'source': '(: f (-> Number Number))\n'
            '(= (f $x) (+ $x 1))\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n',
  'petta': '4\n',
  'he': '[4]\n'},
 {'name': 'first-fails',
  'source': '(: f (-> Number Number))\n'
            '(= (f $x) (empty))\n'
            '(= (f $x) (+ $x 1))\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n',
  'petta': '4\n',
  'he': '[4]\n'},
 {'name': 'duplicate-bodies',
  'source': '(: f (-> Number Number))\n'
            '(= (f $x) $x)\n'
            '(= (f $x) $x)\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n',
  'petta': '3\n3\n',
  'he': '[3, 3]\n'},
 {'name': 'overload',
  'source': '(: f (-> Number Number))\n'
            '(: f (-> String String))\n'
            '(= (f $x) $x)\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n'
            '!(invoke f "text")\n',
  'petta': '3\n"text"\n',
  'he': '[3]\n["text"]\n'},
 {'name': 'raw-result',
  'source': '(: f (-> Atom Atom))\n'
            '(= (f $x) (println! held))\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n',
  'petta': '(println! held)\n',
  'he': '[(println! held)]\n'},
 {'name': 'wrong-input',
  'source': '(: f (-> Number Number))\n'
            '(= (f $x) $x)\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f "wrong")\n',
  'petta': '',
  'he': '[(Error (f "wrong") (BadArgType 1 Number String))]\n'},
 {'name': 'wrong-result',
  'source': '(: f (-> Number Number))\n'
            '(= (f $x) "wrong")\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n',
  'petta': '',
  'he': '[(Error "wrong" (BadType Number String))]\n'},
 {'name': 'effect-result-error',
  'source': '(: f (-> Number Number))\n'
            '(= (f $x) (let $u (println! committed) "wrong"))\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n',
  'petta': 'committed\n',
  'he': 'committed\n[(Error "wrong" (BadType Number String))]\n'},
 {'name': 'duplicate-effects',
  'source': '(: f (-> Number Number))\n'
            '(= (f $x) (let $u (add-atom &self (seen $x)) $x))\n'
            '(= (f $x) (let $u (add-atom &self (seen $x)) $x))\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n'
            '!(collapse (match &self (seen $x) $x))\n',
  'petta': '3\n3\n(3 3)\n',
  'he': '[3, 3]\n[(3 3)]\n'},
 {'name': 'open-head',
  'source': '(= (invoke $f $x) ($f $x))\n!(invoke $head 3)\n',
  'petta': '($head 3)\n',
  'he': '[($head 3)]\n'},
 {'name': 'open-head-effect',
  'source': '(= (invoke $f $x) ($f $x))\n!(invoke $head (println! argument))\n',
  'petta': 'argument\n($head true)\n',
  'he': 'argument\n[($head ())]\n'},
 {'name': 'late-head',
  'source': '(= (inc $x) (+ $x 1))\n'
            '(= (late $h) (let ($a $x) ($h 3) (let $h inc (eval ($a $x)))))\n'
            '!(late $unknown)\n',
  'petta': '4\n',
  'he': '[(+ 3 1)]\n'},
 {'name': 'alias',
  'source': '(: f (-> $t $t Atom))\n'
            '(= (f $a $b) (pair $a $b))\n'
            '(= (invoke $f $a $b) ($f $a $b))\n'
            '!(invoke f $same $same)\n',
  'petta': '(pair $a $a)\n',
  'he': '[(pair $a $a)]\n'},
 {'name': 'declaration-mutation',
  'source': '(= (f $x) $x)\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n'
            '!(add-atom &self (: f (-> Number Number)))\n'
            '!(invoke f 3)\n'
            '!(add-atom &self (unrelated fact))\n'
            '!(invoke f 3)\n',
  'petta': '3\ntrue\n3\ntrue\n3\n',
  'he': '[3]\n[()]\n[3]\n[()]\n[3]\n'},
 {'name': 'body-mutation',
  'source': '(: f (-> Number Number))\n'
            '(= (f $x) $x)\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n'
            '!(add-atom &self (= (f $x) (+ $x 1)))\n'
            '!(invoke f 3)\n',
  'petta': '3\ntrue\n3\n4\n',
  'he': '[3]\n[()]\n[3, 4]\n'},
 {'name': 'native-registry-arguments',
  'source': '!(== 1 1)\n'
            '!(== &self &self)\n',
  'petta': 'true\ntrue\n',
  'he': '[True]\n[True]\n'},
 {'name': 'native-registry-late-type-error',
  'source': '!(== 1 &self)\n',
  'petta': 'false\n',
  'he': '[(Error (== 1 ModuleSpace(GroundingSpace-top)) (BadArgType 2 Number (Space atom)))]\n'},
 {'name': 'native-registry-early-type-error',
  'source': '!(== &self 1)\n',
  'petta': 'false\n',
  'he': '[(Error (== ModuleSpace(GroundingSpace-top) 1) (BadArgType 2 (Space atom) Number))]\n'},
 {'name': 'signature-effects',
  'source': '(: f (-> Number Number))\n'
            '(: f (-> $t Number))\n'
            '(= (f $x) (let $u (println! body) 1))\n'
            '(= (invoke $f $x) ($f $x))\n'
            '!(invoke f 3)\n',
  'petta': 'body\nbody\n1\n1\n',
  'he': 'body\n[1]\n'}]

def normalize(text):
    return '\n'.join(alpha_canonicalize_atom_text(line) for line in text.splitlines())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    profiles = [(['--lang', 'petta'], 'petta'),
                (['--lang', 'petta', '--profile', 'extended'], 'petta'),
                (['--lang', 'he'], 'he'),
                (['--lang', 'he', '--profile', 'he-compat'], 'he'),
                (['--lang', 'he', '--profile', 'extended'], 'he')]
    observations = []
    for case in CASES:
        source = output / (case['name'] + '.metta')
        source.write_text(case['source'])
        for options, dialect in profiles:
            for machine, reference in [('0', '1'), ('1', '0'), ('1', '1')]:
                run = subprocess.run([str(binary), *options, str(source)],
                    cwd=output, env=dict(os.environ,
                        CETTA_PETTA_SEARCH_MACHINE=machine,
                        CETTA_OPEN_EQUATIONS_REFERENCE=reference),
                    text=True, capture_output=True, timeout=30)
                expected = (0, normalize(case[dialect]), '')
                actual = (run.returncode, normalize(run.stdout), run.stderr)
                observations.append(dict(case=case['name'], options=options,
                    machine=machine, reference=reference, correct=actual == expected,
                    rc=run.returncode, stdout=run.stdout, stderr=run.stderr))
                if actual != expected:
                    (output / 'observations.json').write_text(json.dumps(observations, indent=2) + '\n')
                    raise AssertionError((case['name'], options, machine, reference, actual, expected))
    (output / 'observations.json').write_text(json.dumps(dict(
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        observations=observations), indent=2) + '\n')
    print(f'PASS: {len(observations)} dynamic call observations preserve guards, '
          'duplicate answers, effects, live declarations and open-head bindings')


if __name__ == '__main__':
    main()
