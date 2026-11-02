#!/usr/bin/env python3
"""Opt-in Prover9 unit-equation data experiment using the ordinary IC1 checker.

The adapter admits only unambiguous first-order unit equations. Other proof
records are counted, not interpreted as equations or silently discarded.
This mines syntactic templates; it neither checks a Prover9 derivation nor
establishes that a proposed hint is a theorem or useful for proof search.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import signal
import subprocess
import time

from check_binary_codec import ROOT, parse_forms
from check_binary_tree import wire
from check_compact_dag import records_wire
from check_ic_search import check_controls


class Unsupported(ValueError):
    pass


TOKEN = re.compile(r"\s*([A-Za-z_$][A-Za-z0-9_$]*|[0-9]+|[(),*/\\=])")
RECORD = re.compile(r"^\s*(\d+)\s+(.+)\.\s{2,}\[(.*)\]\.$")


def equation(text):
    """Reject unparenthesized operator chains rather than guess precedence."""
    tokens = []
    rest = text.strip()
    while rest:
        match = TOKEN.match(rest)
        if not match:
            raise Unsupported('unsupported-token')
        tokens.append(match[1])
        rest = rest[match.end():]
    position = 0

    def atom(depth):
        nonlocal position
        if depth > 128:
            raise Unsupported('term-depth')
        if position == len(tokens):
            raise Unsupported('incomplete-term')
        token = tokens[position]
        position += 1
        if token == '(':
            result = term(depth + 1)
            if position == len(tokens) or tokens[position] != ')':
                raise Unsupported('parenthesis-or-ambiguous-chain')
            position += 1
            return result
        if not re.fullmatch(r'[A-Za-z_$][A-Za-z0-9_$]*|[0-9]+', token):
            raise Unsupported('expected-identifier')
        if position == len(tokens) or tokens[position] != '(':
            return ('leaf', token.encode())
        position += 1
        children = [term(depth + 1)]
        while position < len(tokens) and tokens[position] == ',':
            position += 1
            children.append(term(depth + 1))
        if position == len(tokens) or tokens[position] != ')':
            raise Unsupported('function-arguments')
        position += 1
        return ('node', token.encode(), tuple(children))

    def term(depth):
        nonlocal position
        left = atom(depth)
        if position < len(tokens) and tokens[position] in ('*', '/', '\\'):
            operator = tokens[position]
            position += 1
            return ('node', operator.encode(), (left, atom(depth)))
        return left

    left = term(0)
    if position == len(tokens) or tokens[position] != '=':
        raise Unsupported('not-unit-or-ambiguous-chain')
    position += 1
    right = term(0)
    if position != len(tokens):
        raise Unsupported('trailing-or-ambiguous-chain')
    return ('node', b'=', (left, right))


def render(tree):
    if tree[0] == 'leaf':
        return tree[1].decode()
    _, tag, children = tree
    if tag in (b'=', b'*', b'/', b'\\'):
        return '(' + render(children[0]) + ' ' + tag.decode() + ' ' + render(children[1]) + ')'
    return tag.decode() + '(' + ','.join(map(render, children)) + ')'


def shape(tree, depth):
    if depth == 0 or tree[0] == 'leaf':
        return ('hole',)
    return tree[1], tuple(shape(child, depth - 1) for child in tree[2])


def size(tree):
    return 1 if tree[0] == 'leaf' else 1 + sum(map(size, tree[2]))


@dataclass
class Row:
    line: int
    number: int
    formula: str
    inference: str
    tree: tuple


def read_proof(path):
    rows, skipped = [], Counter()
    proof_records = 0
    with path.open(encoding='utf-8') as source:
        for line, text in enumerate(source, 1):
            match = RECORD.fullmatch(text.rstrip('\n'))
            if not match:
                if re.match(r'^\s*\d+\s', text):
                    proof_records += 1
                    skipped['unsupported-record-format'] += 1
                continue
            proof_records += 1
            formula = match[2].split('#', 1)[0].strip()
            try:
                tree = equation(formula)
            except (Unsupported, RecursionError) as failure:
                skipped[str(failure) if isinstance(failure, Unsupported) else 'term-depth'] += 1
                continue
            rows.append(Row(line, int(match[1]), formula, match[3], tree))
    return rows, proof_records, skipped


def dag(trees):
    records, indexes = [], {}

    def visit(tree):
        if tree in indexes:
            return indexes[tree]
        children = () if tree[0] == 'leaf' else tuple(map(visit, tree[2]))
        index = len(records)
        records.append((tree[0], tree[1], children))
        indexes[tree] = index
        return index

    return [visit(tree) for tree in trees], records


def choose(rows, count):
    groups = defaultdict(list)
    for row in rows:
        if size(row.tree) >= 12:
            groups[shape(row.tree, 3)].append(row)
    if not groups:
        raise ValueError('no sufficiently structured unit-equation group')
    # Selection uses syntax alone, not successful template-installation results.
    return max(groups.values(), key=lambda group: (len(group), -group[0].line))[:count]


NATIVE = r'''
!(import! &self (library lib_ic_tree_archive))
(= (proof-pilot:pair $forest $archive $left $right)
   (case (ic-archive:learn-pair $archive $archive "proof-template" $left $right)
     (((IC:ArchiveAccepted $candidate $bytes $before $after)
       (ProofCandidate $left $right Accepted $before $after
         (== (ic:restore-trees Constructors $candidate) (IC:Some $forest))
         (== (index-atom (tensor:shape $bytes) 0) $after)
         (index-atom $candidate 6)))
      ((IC:ArchiveRejected $reason) (ProofCandidate $left $right Rejected $reason))
      ($other (ProofCandidate $left $right Fault $other)))))
(= (proof-pilot:archive $forest $pairs)
   (case (ic:prepare-trees Constructors $forest)
     (((IC:ConstructorArchive $archive)
       (let* (($roundtrip (println! (ProofRoundTrip
                 (== (ic:restore-trees Constructors $archive) (IC:Some $forest)))))
              ($results (map-atom $pairs $pair
                 (proof-pilot:pair $forest $archive (car-atom $pair) (index-atom $pair 1)))))
         (println! (ProofCandidates $results))))
      ($other (println! (ProofFault $other))))))
'''


def run_native(binary, output, name, source, timeout, fuel):
    path = output / (name + '.metta')
    path.write_text(source, encoding='utf-8')
    rss = output / (name + '.peak-kib')
    started = time.monotonic()
    process = subprocess.Popen(['/usr/bin/time', '-f', '%M', '-o', str(rss.resolve()),
                                str(binary.resolve()), '--lang', 'petta', '--fuel', str(fuel), str(path.resolve())],
                               cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               start_new_session=True)
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        os.killpg(process.pid, signal.SIGKILL)
        stdout, stderr = process.communicate()
    (output / (name + '.log')).write_text(stdout + stderr, encoding='utf-8')
    peak = rss.read_text().strip().splitlines()[-1:] if rss.exists() else []
    return {'exit_code': process.returncode, 'timed_out': timed_out,
            'elapsed_seconds': time.monotonic() - started,
            'peak_rss_kib': int(peak[0]) if peak and peak[0].isdigit() else None,
            'observations': parse_forms(stdout)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--source-url', default='')
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rows', type=int, default=24)
    parser.add_argument('--pairs', type=int, default=3)
    parser.add_argument('--timeout', type=float, default=120)
    parser.add_argument('--fuel', type=int, default=20000000)
    args = parser.parse_args()
    if args.rows < 2 or args.pairs < 1 or args.timeout <= 0 or args.fuel <= 0:
        parser.error('rows >= 2, pairs >= 1, timeout > 0 and fuel > 0 are required')
    args.output.mkdir(parents=True, exist_ok=True)
    source_digest = hashlib.sha256(args.proof.read_bytes()).hexdigest()
    binary_digest = hashlib.sha256(args.cetta.read_bytes()).hexdigest()
    started = time.monotonic()
    rows, total, skipped = read_proof(args.proof)
    if not rows:
        raise ValueError('no admitted unit equations')
    selected = choose(rows, args.rows)
    if len(selected) < 2:
        raise ValueError('fewer than two selected equations')
    # Round-trip the admitted syntax without renaming variables or dropping edges.
    assert all(equation(render(row.tree)[1:-1]) == row.tree for row in rows)
    roots, records = dag([row.tree for row in rows])
    compact = records_wire(roots, records)
    dag_source = '''!(import! &self (library dag))
!(import! &self (library tensor))
!(let $handle (dag:from-records ''' + compact + ''')
   (let* (($stats (println! (ProofDag (dag:stats $handle))))
          ($closed (dag:close $handle)))
     (println! (ProofDagClosed (== $closed ()) (dag:stats $handle)))))
'''
    forest = '(' + ' '.join(wire(row.tree) for row in selected) + ')'
    pairs = list(dict.fromkeys([(0, len(selected) - 1), (0, 1)] +
                              [(index, index + 1) for index in range(1, len(selected) - 1)]))[:args.pairs]
    pairs_wire = '(' + ' '.join(f'({left} {right})' for left, right in pairs) + ')'
    ic_source = NATIVE + f'\n!(proof-pilot:archive {forest} {pairs_wire})\n'
    check_controls(ic_source)
    preparation = time.monotonic() - started
    native_dag = run_native(args.cetta, args.output, 'dag', dag_source, args.timeout, args.fuel)
    native_ic = run_native(args.cetta, args.output, 'ic', ic_source, args.timeout, args.fuel)
    dag_forms = native_dag['observations']
    ic_forms = native_ic['observations']
    stats = [row[1] for row in dag_forms if isinstance(row, list) and row and row[0] == 'ProofDag']
    closed = ['ProofDagClosed', 'true', ['Dag:Fault', 'Released']] in dag_forms
    results = [row[1] for row in ic_forms if isinstance(row, list) and row and row[0] == 'ProofCandidates']
    candidates = results[0] if len(results) == 1 else []
    valid_candidates = len(candidates) == len(pairs) and all(
        row[0] == 'ProofCandidate' and row[1:3] == list(map(str, pair)) and
        (row[3:] == ['Rejected', 'NotSmaller'] or row[3] == 'Accepted' and
         int(row[5]) < int(row[4]) and row[6:8] == ['true', 'true'])
        for row, pair in zip(candidates, pairs))
    dependencies_current = (source_digest == hashlib.sha256(args.proof.read_bytes()).hexdigest()
                            and binary_digest == hashlib.sha256(args.cetta.read_bytes()).hexdigest())
    passed = (dependencies_current
              and all(run['exit_code'] == 0 and not run['timed_out'] for run in (native_dag, native_ic))
              and len(stats) == 1 and stats[0][0:3] == ['Dag:Stats', str(len(records)),
                  str(sum(len(record[2]) for record in records))] and closed
              and ['ProofRoundTrip', 'true'] in ic_forms and valid_candidates)
    receipt = {'passed': bool(passed), 'source_url': args.source_url,
               'source_sha256': source_digest, 'cetta_sha256': binary_digest,
               'dependencies_unchanged': dependencies_current,
               'source_bytes': args.proof.stat().st_size, 'proof_records': total,
               'fuel_per_native_run': args.fuel,
               'admitted_unit_equations': len(rows), 'unsupported_records': dict(skipped),
               'unfolded_nodes': sum(size(row.tree) for row in rows), 'dag_records': len(records),
               'dag_edges': sum(len(record[2]) for record in records),
               'selected': [{'line': row.line, 'clause': row.number, 'formula': row.formula,
                             'inference': row.inference} for row in selected],
               'native_dag': native_dag, 'native_ic': native_ic,
               'adapter_seconds': preparation, 'adapter_peak_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
               'scope': 'exact admitted syntax; bounded pair proposals; complete IC1 cost and reconstruction; '
                        'DAG-owned retained bytes are not whole-process retention; '
                        'no derivation validation, ILP discovery or hint-quality claim'}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({key: receipt[key] for key in ('passed', 'source_bytes', 'proof_records',
          'admitted_unit_equations', 'unfolded_nodes', 'dag_records', 'adapter_peak_rss_kib')}))
    print(json.dumps({'dag_peak_rss_kib': native_dag['peak_rss_kib'], 'dag_stats': stats,
                      'ic_peak_rss_kib': native_ic['peak_rss_kib'], 'candidates': candidates}))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
