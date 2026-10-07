#!/usr/bin/env python3
"""Check tutorial answers and larger proof joins without collapsing evidence."""
import argparse
from collections import defaultdict
import itertools
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
EXAMPLES = ROOT / 'examples/chaining/tutorial'


def parse(text):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', text)
    stack, value = [], None
    for token in tokens:
        if token == '(':
            stack.append([])
        elif token == ')':
            assert stack, text
            item = tuple(stack.pop())
            if stack:
                stack[-1].append(item)
            else:
                assert value is None, text
                value = item
        else:
            item = token.lower() if token in ('True', 'False') else token
            if stack:
                stack[-1].append(item)
            else:
                assert value is None, text
                value = item
    assert not stack and value is not None, text
    return value


def alpha(value):
    names = {}

    def walk(term):
        if isinstance(term, tuple):
            return tuple(walk(child) for child in term)
        return names.setdefault(term, f'$v{len(names)}') if term.startswith('$') else term
    return walk(value)


def run(binary, language, *, path=None, source=None, profile='extended', reference=False):
    command = [str(binary), '--lang', language]
    if profile:
        command += ['--profile', profile]
    command += [str(path)] if path else ['-e', source]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
        env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE='1',
                 CETTA_OPEN_EQUATIONS_REFERENCE=str(int(reference)),
                 CETTA_HE_SHARED_EXECUTION=str(int(not reference))), timeout=120)
    assert (result.returncode, result.stderr) == (0, ''), (language, path, result.returncode, result.stderr)
    lines = result.stdout.splitlines()
    if language == 'he':
        assert all(line.startswith('[') and line.endswith(']') for line in lines), lines
        lines = [line[1:-1] for line in lines]
    return [parse(line) for line in lines]


def proof_path():
    def fact(label, left, right):
        return f'(proof {label} (edge {left} {right}))'
    last = f'(proof (direct ({fact("cd", "carol", "dana")})) (reachable carol dana))'
    middle = f'(proof (step ({fact("bc", "bob", "carol")} {last})) (reachable bob dana))'
    return f'(proof (step ({fact("ab", "alice", "bob")} {middle})) (reachable alice dana))'


DIRECT = '(proof (direct ((proof ab (edge alice bob)))) (reachable alice bob))'
EXPECTED = {
    '01-backward': (2, [f'({proof_path()})', '()', f'({DIRECT})', '()']),
    '02-direction': (1, ['((destination b) (destination b) (destination b))',
        '((destination b) (destination b))',
        '((covered (edge $s b)) (covered (edge a b)) (covered (edge a b)))',
        '12', '(kept $u)', '(same a)', 'different', 'true', 'false', 'true', 'false']),
    '03-forward': (1, ['done', '(bob carol)', 'done', '(bob carol dana)',
                       'done', '(bob carol dana)', '()']),
    '04-gates': (3, [f'({proof_path()})', '()', f'({DIRECT})']),
    '05-joint': (1, ['((grandparent alice carol))', '(proof a b)', '()', '(separate a)',
                     '(Bindings (($x a) ($y b)) ())']),
    '07-loop-tests': (1, ['true', 'false', 'false', 'true']),
    '08-tables.petta': (0, ['true', 'true', '6765', 'true', '(proof)', '(proof proof)',
                           '()', 'true', 'true', '81', '81']),
    '09-nil-size.petta': (1, ['()', '((MkSized 1 (: ax₁ (→ A (→ B A)))))', '()',
                             '((MkSized 5 (: (mp (mp ax₂ ax₁) ax₁) (→ A A))))']),
}


def check_evidence(answers):
    assert len(answers) == 3 and answers[-1] == ()
    proofs = answers[1]
    assert len(proofs) == 2
    first_ids, second_ids = [], []
    for proof in proofs:
        assert proof[:2] == ('derivation', ('reachable', 'alice', 'carol'))
        rows = proof[2]
        assert len(rows) == 2
        for row, label, left, right in zip(rows, ('sensor-a', 'sensor-b'),
                                         ('alice', 'bob'), ('bob', 'carol')):
            assert row[0] == 'pat:row' and row[1][0] == 'pat:occurrence'
            assert row[2] == row[3] == ('evidence', label, ('edge', left, right))
        first_ids.append(rows[0][1]); second_ids.append(rows[1][1])
    assert first_ids[0] != first_ids[1] and second_ids[0] == second_ids[1]
    assert first_ids[0][1:3] == first_ids[1][1:3] == second_ids[0][1:3]


def larger_proofs(binary, language, reference):
    chains, legs = 64, 12
    duplicate_legs = (0, 7)
    facts = []
    for chain in range(chains):
        for leg in range(legs):
            row = f'(p{leg} (node {chain} {leg}) (node {chain} {leg + 1}))'
            facts.extend([row] * (2 if leg in duplicate_legs else 1))
    patterns = ' '.join(f'(p{i} $v{i} $v{i+1})' for i in range(legs))
    values = ' '.join(f'$v{i}' for i in range(legs + 1))
    query = (f'!(collapse (let (pat:hit $rows $w) '
             f'(pat:query &self match% (, {patterns})) '
             f'(proof $rows (pat:apply $w ({values})))))')
    # An open stored prefix must not become a fabricated extra chain.
    facts += ['(p0 $protected (node missing 1))', '(p1 (node missing 1) absent)']
    answers = run(binary, language, source='!(import! &self pat)\n' + '\n'.join(facts) + '\n' + query,
                  reference=reference)
    assert len(answers) == 2
    proofs = answers[1]
    assert len(proofs) == chains * 4
    certificates = defaultdict(set)
    occurrence_values, value_occurrences = {}, defaultdict(set)
    endpoints = []
    for proof in proofs:
        assert proof[0] == 'proof' and len(proof[1]) == legs
        rows, nodes = proof[1:]
        chain = int(nodes[0][1]); endpoints.append(chain)
        assert nodes == tuple(('node', str(chain), str(i)) for i in range(legs + 1))
        ids = []
        for leg, row in enumerate(rows):
            tag, identity, original, view = row
            assert tag == 'pat:row' and identity[0] == 'pat:occurrence'
            wanted = (f'p{leg}', nodes[leg], nodes[leg+1])
            assert original == view == wanted
            assert occurrence_values.setdefault(identity, wanted) == wanted
            value_occurrences[wanted].add(identity)
            ids.append(identity)
        certificates[chain].add(tuple(ids))
    assert endpoints == [chain for chain in range(chains) for _ in range(4)]
    for chain in range(chains):
        choices = []
        for leg in range(legs):
            key = (f'p{leg}', ('node', str(chain), str(leg)), ('node', str(chain), str(leg + 1)))
            ids = value_occurrences[key]
            assert len(ids) == (2 if leg in duplicate_legs else 1)
            choices.append(ids)
        assert certificates[chain] == set(itertools.product(*choices))
    return len(proofs), len(proofs) * legs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    args = parser.parse_args(); binary = args.binary.resolve()
    checked = proofs = rows = 0
    for language in ('he', 'petta'):
        for reference in (False, True):
            for name, (skip, expected) in EXPECTED.items():
                if '.petta' in name and language != 'petta':
                    continue
                answers = run(binary, language, path=EXAMPLES / f'{name}.metta', reference=reference)
                assert [alpha(x) for x in answers[skip:]] == [alpha(parse(x)) for x in expected], (language, reference, name, answers)
                checked += 1
            check_evidence(run(binary, language, path=EXAMPLES / '06-evidence.metta', reference=reference))
            checked += 1
            count, n = larger_proofs(binary, language, reference)
            proofs += count; rows += n
        if language == 'petta':
            for name in ('01-backward', '08-tables.petta', '09-nil-size.petta'):
                skip, expected = EXPECTED[name]
                answers = run(binary, language, path=EXAMPLES / f'{name}.metta', profile=None)
                assert [alpha(x) for x in answers[skip:]] == [alpha(parse(x)) for x in expected]
                checked += 1
    assert alpha(parse('(pair $x $x)')) != alpha(parse('(pair $x $y)'))
    assert alpha(parse('(text "$x")')) != alpha(parse('(text "$y")'))
    assert parse('(a a)') != parse('(a)')
    print(f'PASS: {checked} tutorial runs; {proofs} larger proofs and {rows} evidence rows checked')


if __name__ == '__main__':
    main()
