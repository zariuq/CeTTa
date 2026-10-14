#!/usr/bin/env python3
"""Validate and display an artifact-scoped regional benchmark ledger.

Results identify the observer, phase and checked contract as well as the
engine. A count of derivations is not a stored-atom capacity measurement.
Correctness qualifications attach to one artifact and one scope; relinking
a binary does not inherit them. Evidence files are relative to the ledger.

Usage: result_ledger.py LEDGER.json [--verify-evidence] [--csv FILE]
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re


METRICS = ('wall_s', 'query_ms', 'instructions', 'peak_rss_mib', 'retained_bytes')
OBSERVERS = {'ordered_occurrences', 'set', 'count', 'exists', 'effect_history'}
PHASES = {'whole_process', 'preparation', 'query', 'transfer', 'scheduling', 'validation'}
QUALITIES = {'checked_samples', 'diagnostic', 'exploratory', 'pending'}
STATUSES = {'passed', 'failed', 'pending'}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def validate(ledger, evidence_root=None):
    require(ledger['schema'] == 'cetta-regional-result-ledger-v1', 'unknown ledger schema')
    artifacts, evidence = ledger['artifacts'], ledger['evidence']
    require(bool(artifacts), 'no artifact identities')
    for name, artifact in artifacts.items():
        require(identity(artifact['sha256']), name + ': invalid artifact identity')
        require(artifact['kind'] in ('binary', 'source_bundle'), name + ': unknown artifact kind')
    for name, record in evidence.items():
        path = PurePosixPath(record['file'])
        require(not path.is_absolute() and '..' not in path.parts and path.parts,
                name + ': evidence must be a relative bundle path')
        require(identity(record['sha256']), name + ': invalid evidence identity')
        if evidence_root is not None:
            actual = hashlib.sha256((evidence_root / path).read_bytes()).hexdigest()
            require(actual == record['sha256'], name + ': evidence changed')

    def references(records):
        require(bool(records) and all(name in evidence for name in records),
                'missing evidence reference')

    for qualification in ledger['qualifications']:
        require(qualification['artifact'] in artifacts, 'qualification has no artifact')
        require(qualification['status'] in STATUSES, 'invalid qualification status')
        require(bool(qualification['scope']), 'qualification has no scope')
        if qualification['status'] != 'pending':
            references(qualification['evidence'])

    names = set()
    for result in ledger['results']:
        name = result['id']
        require(name not in names, 'duplicate result id: ' + name)
        names.add(name)
        require(result['observer'] in OBSERVERS, name + ': unknown observer')
        require(result['phase'] in PHASES, name + ': unknown phase')
        require(result['quality'] in QUALITIES, name + ': unknown measurement quality')
        references(result['evidence'])
        comparison = result['comparison']
        require(comparison in ('matched', 'unresolved', 'none'), name + ': unknown comparison')
        require(bool(result['variants']), name + ': no measured variants')
        variant_names = set()
        contracts = set()
        for variant in result['variants']:
            require(variant['name'] not in variant_names, name + ': duplicate variant')
            variant_names.add(variant['name'])
            require(variant['artifact'] in artifacts, name + ': unidentified variant artifact')
            require(variant['correct'] in STATUSES, name + ': invalid observation status')
            require(identity(variant['contract_sha256']), name + ': missing checked contract identity')
            contracts.add(variant['contract_sha256'])
            trials = variant['trials']
            require(type(trials) is int and trials >= 0, name + ': invalid trial count')
            for metric, value in variant['metrics'].items():
                require(metric in METRICS, name + ': unknown metric ' + metric)
                require(value is None or (type(value) in (int, float) and math.isfinite(value)
                                         and value >= 0), name + ': invalid metric')
                require(value is None or trials > 0, name + ': measurement without trials')
            if result['quality'] == 'checked_samples':
                require(variant['correct'] == 'passed' and trials > 0,
                        name + ': unchecked sample cannot be a checked measurement')
        if comparison == 'matched':
            require(len(contracts) == 1 and len(variant_names) >= 2,
                    name + ': comparison changes the observation contract')
            require(all(v['correct'] == 'passed' for v in result['variants']),
                    name + ': failed observations cannot be a matched comparison')
    for finding in ledger['findings']:
        require(type(finding['priority']) is int and finding['priority'] > 0,
                'invalid finding priority')
        require(finding['status'] in ('open', 'resolved', 'deferred', 'experimental'),
                'invalid finding status')
        require(bool(finding['next']) and bool(finding['controls']), 'finding has no next check')
        references(finding['evidence'])


def ratios(result, metric):
    """Ratios compare checked work in the same phase and observation contract.
    They are descriptive, never a replacement for the calibrated cost gate."""
    if result['comparison'] != 'matched':
        return {}
    baseline = result['variants'][0]
    value = baseline['metrics'].get(metric)
    if value is None or value <= 0:
        return {}
    return {v['name']: v['metrics'][metric] / value for v in result['variants'][1:]
            if v['metrics'].get(metric) is not None}


def render(ledger):
    lines = ['CeTTa regional result ledger',
             'Ratios are descriptive; scopes and artifact qualifications remain separate.', '']
    for q in ledger['qualifications']:
        lines.append(f"{q['status']:7} {q['artifact']}: {q['scope']}")
    for result in ledger['results']:
        lines += ['', f"{result['id']} [{result['quality']}; {result['comparison']}]",
                  f"  {result['observer']} / {result['phase']}: {result['task']}"]
        for variant in result['variants']:
            metrics = ', '.join(f'{k}={v:.6g}' for k, v in variant['metrics'].items()
                                if v is not None)
            lines.append(f"  {variant['name']} ({variant['artifact']}, {variant['trials']} trials): {metrics}")
        if result.get('limits'):
            lines.append('  Limits: ' + result['limits'])
    lines += ['', 'Next experiments, in priority order:']
    for finding in sorted(ledger['findings'], key=lambda row: row['priority']):
        lines += [f"{finding['priority']}. [{finding['status']}] {finding['title']}",
                  '   ' + finding['next'], '   Controls: ' + finding['controls']]
    return '\n'.join(lines) + '\n'


def write_csv(ledger, stream):
    fields = ['result', 'quality', 'comparison', 'observer', 'phase', 'variant',
              'artifact', 'correct', 'trials', *METRICS]
    writer = csv.DictWriter(stream, fieldnames=fields)
    writer.writeheader()
    for result in ledger['results']:
        for variant in result['variants']:
            writer.writerow(dict(result=result['id'], quality=result['quality'],
                comparison=result['comparison'], observer=result['observer'], phase=result['phase'],
                variant=variant['name'], artifact=variant['artifact'], correct=variant['correct'],
                trials=variant['trials'], **variant['metrics']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ledger', type=Path)
    parser.add_argument('--verify-evidence', action='store_true')
    parser.add_argument('--csv', type=Path)
    args = parser.parse_args()
    ledger = json.loads(args.ledger.read_text())
    validate(ledger, args.ledger.parent if args.verify_evidence else None)
    if args.csv:
        with args.csv.open('w', newline='') as stream:
            write_csv(ledger, stream)
    print(render(ledger), end='')


if __name__ == '__main__':
    main()
