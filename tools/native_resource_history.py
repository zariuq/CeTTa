#!/usr/bin/env python3
"""Read requested snapshot-world histories without executing their code.

The ground-bag-read-take-add-v1 profile checks each recorded firing against
supplied initial physical rows. It exposes an ordered occurrence word and a
causal readout that forgets only independent reorderings and final row order.
This is a debugging observation of data, not execution or replay authority.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import heapq
import json
from pathlib import Path
import subprocess
from typing import Any

from mm2_to_metta import parse

PROFILE = 'ground-bag-read-take-add-v1'
Key = tuple[int, ...]


class HistoryRefused(ValueError):
    """The supplied word is inconsistent or outside the selected profile."""


def _fields(rows: list) -> dict:
    result = {}
    for row in rows:
        if not isinstance(row, list) or len(row) != 2 or not isinstance(row[0], str):
            raise HistoryRefused('MalformedField')
        if row[0] in result:
            raise HistoryRefused('DuplicateField')
        result[row[0]] = row[1]
    return result


def _quote(value: Any) -> Any:
    if not isinstance(value, list) or len(value) != 2 or value[0] != 'quote':
        raise HistoryRefused('ExpectedQuotedData')
    return value[1]


def _natural(value: Any) -> int:
    if not isinstance(value, (str, int)) or isinstance(value, bool):
        raise HistoryRefused('ExpectedNatural')
    text = str(value)
    if not text.isascii() or not text.isdecimal():
        raise HistoryRefused('ExpectedNatural')
    return int(text)


def _ground(value: Any) -> bool:
    if isinstance(value, list):
        if value and value[0] in ('NativeHandle', 'NeedRef', 'StateCell'):
            return False
        return all(_ground(item) for item in value)
    # This reader preserves literal tokens, including quoted strings. It does
    # not turn a string token into a symbol or execute an expression.
    return (isinstance(value, str) and not value.startswith(('$', '&')) and
            (value.startswith(chr(34)) or not any(c in value for c in '[],')))


def _word(trace: Any) -> list:
    reverse = []
    while trace != []:
        if not isinstance(trace, list) or len(trace) != 3 or trace[0] != 'then':
            raise HistoryRefused('UnsupportedTraceShape')
        reverse.append(trace[2])
        trace = trace[1]
    return list(reversed(reverse))


@dataclass(frozen=True)
class _Row:
    key: Key
    value: Any


@dataclass
class _Firing:
    key: Key
    action: Any
    inputs: list[tuple[str, Key]]
    consumed: set[Key]
    read: set[Key]
    produced: list[_Row]

    def record(self) -> dict:
        return {'occurrence': self.key, 'action': self.action,
                'inputs': self.inputs, 'consumed': sorted(self.consumed),
                'read': sorted(self.read),
                'produced': [(row.key, row.value) for row in self.produced]}


def read_history(initial: list, world: list) -> dict:
    """Check a finite ground firing word and construct its two observations.

    Occurrence keys are initial row indices, extended by output slots when a
    firing creates rows. Equal syntax never identifies two physical rows.
    No unsupported input/sink is approximated by a smaller footprint.
    """
    if not isinstance(initial, list) or not all(_ground(row) for row in initial):
        raise HistoryRefused('UnsupportedInitialRows')
    if not isinstance(world, list) or len(world) != 6 or world[0] != 'SnapshotWorld':
        raise HistoryRefused('ExpectedSnapshotWorld')
    if _natural(world[1]) == 0:
        raise HistoryRefused('MalformedWorldIdentity')
    fields = _fields(world[2:])
    if set(fields) != {'atoms', 'trace', 'source', 'state'} or fields['state'] != 'Quiescent':
        raise HistoryRefused('UnsupportedWorldState')
    source = fields['source']
    if not isinstance(source, list) or len(source) != 2 or _natural(source[0]) == 0:
        raise HistoryRefused('MalformedSource')
    _natural(source[1])
    rows = [_Row((i,), value) for i, value in enumerate(initial)]
    firings = []
    target = None
    seen_worlds = set()
    for step in _word(_quote(fields['trace'])):
        if not isinstance(step, list) or len(step) != 10 or step[0] != 'WorldStep':
            raise HistoryRefused('ExpectedWorldStep')
        meta = _fields(step[2:])
        if set(meta) != {'environments', 'dependencies', 'retries', 'store', 'order',
                         'from', 'to', 'selected-occurrence'}:
            raise HistoryRefused('UnsupportedStepFields')
        if meta['store'] != 'bag' or _natural(meta['environments']) != 1:
            raise HistoryRefused('UnsupportedStoreOrEnvironmentCount')
        _natural(meta['retries'])
        begin, end = _natural(meta['from']), _natural(meta['to'])
        if not begin or not end or begin == end or end in seen_worlds or (target is not None and begin != target):
            raise HistoryRefused('BrokenWorldChain')
        if not seen_worlds:
            seen_worlds.add(begin)
        seen_worlds.add(end)
        target = end
        selected = _natural(meta['selected-occurrence'])
        action = _quote(step[1])
        if selected >= len(rows) or rows[selected].value != action:
            raise HistoryRefused('SelectedOccurrenceMismatch')
        if not _ground(action) or len(action) != 4 or action[0] != 'exec':
            raise HistoryRefused('UnsupportedExecution')
        inputs, outputs = action[2:]
        if not isinstance(inputs, list) or not inputs or inputs[0] != 'I' or \
                not isinstance(outputs, list) or not outputs or outputs[0] != 'O':
            raise HistoryRefused('UnsupportedInputOrOutput')
        dependencies = _quote(meta['dependencies'])
        if not isinstance(dependencies, list) or len(dependencies) != 1 or \
                not isinstance(dependencies[0], list) or len(dependencies[0]) != len(inputs) - 1:
            raise HistoryRefused('InputDependencyMismatch')
        consume_indices = {selected}
        read_indices = set()
        observed_inputs = []
        for premise, dep in zip(inputs[1:], dependencies[0]):
            if not isinstance(premise, list) or len(premise) != 2 or premise[0] not in ('read', 'take'):
                raise HistoryRefused('UnsupportedInput')
            if not isinstance(dep, list) or len(dep) != 4 or dep[:2] != premise or dep[3] != premise[1]:
                raise HistoryRefused('DependencyMismatch')
            index = _natural(dep[2])
            if index >= len(rows) or rows[index].value != dep[3]:
                raise HistoryRefused('DependencyOccurrenceMismatch')
            if premise[0] == 'take':
                if index in consume_indices:
                    raise HistoryRefused('RepeatedConsumption')
                consume_indices.add(index)
            else:
                read_indices.add(index)
            observed_inputs.append((premise[0], rows[index].key))
        # A read of the very occurrence being consumed has a different source
        # contract. Do not silently count one occurrence as two resources.
        if consume_indices & read_indices:
            raise HistoryRefused('UnsupportedReadConsumeAlias')
        created = []
        for slot, sink in enumerate(outputs[1:]):
            if not isinstance(sink, list) or len(sink) != 2 or sink[0] != '+':
                raise HistoryRefused('UnsupportedSink')
            created.append(_Row(rows[selected].key + (slot,), sink[1]))
        firing = _Firing(rows[selected].key, action, observed_inputs,
                         {rows[i].key for i in consume_indices},
                         {rows[i].key for i in read_indices}, created)
        firings.append(firing)
        rows = [row for i, row in enumerate(rows) if i not in consume_indices] + created
    if target is not None and target != _natural(world[1]):
        raise HistoryRefused('FinalWorldMismatch')
    if [row.value for row in rows] != _quote(fields['atoms']):
        raise HistoryRefused('FinalRowsMismatch')

    # Per-resource producer/reader frontiers retain conflicts and availability.
    # Work follows observed resource uses and emitted edges, not all event pairs.
    # A least-key topological readout is independent of the order of unrelated
    # firings. Original words remain separately available for ordered clients.
    parents = []
    children = [[] for _ in firings]
    producers: dict[Key, int] = {}
    readers: dict[Key, set[int]] = {}
    for index, firing in enumerate(firings):
        incoming = {producers[key] for key in firing.consumed | firing.read if key in producers}
        for key in firing.consumed:
            incoming.update(readers.pop(key, ()))
            producers.pop(key, None)
        for key in firing.read:
            readers.setdefault(key, set()).add(index)
        for row in firing.produced:
            producers[row.key] = index
        parents.append(incoming)
        for parent in incoming:
            children[parent].append(index)
    remaining = [len(incoming) for incoming in parents]
    ready = [(event.key, i) for i, event in enumerate(firings) if not remaining[i]]
    heapq.heapify(ready)
    canonical = []
    while ready:
        _, index = heapq.heappop(ready)
        canonical.append({**firings[index].record(),
                          'parents': sorted(firings[p].key for p in parents[index])})
        for child in children[index]:
            remaining[child] -= 1
            if remaining[child] == 0:
                heapq.heappush(ready, (firings[child].key, child))
    assert len(canonical) == len(firings)
    return {'profile': PROFILE, 'authority': 'None',
            'ordered': [firing.record() for firing in firings],
            'final': [(row.key, row.value) for row in rows],
            'causal': {'source': source, 'initial': initial, 'events': canonical,
                       'final': [(row.key, row.value) for row in sorted(rows, key=lambda row: row.key)]}}


def resource_cost(observation: dict) -> dict:
    """Count source firings and their longest resource-dependency chain.

    Each represented firing contributes one. Matching attempts, indexing,
    elapsed time and scheduler/worker ordering are outside this profile.
    Costs cover this supplied interval; its initial resources are not charged.
    Separate worlds can share a prefix, so their costs are not additive.
    The input is the checked data returned by read_history.
    """
    if observation.get('profile') != PROFILE:
        raise HistoryRefused('UnsupportedCostProfile')
    finishes: dict[Key, int] = {}
    for event in observation['causal']['events']:
        key = tuple(event['occurrence'])
        if key in finishes:
            raise HistoryRefused('DuplicateCostOccurrence')
        parents = [tuple(parent) for parent in event['parents']]
        if any(parent not in finishes for parent in parents):
            raise HistoryRefused('CostPredecessorUnavailable')
        finishes[key] = 1 + max((finishes[parent] for parent in parents), default=0)
    return {'profile': 'resource-firings-v1', 'work': len(finishes),
            'dependencyDepth': max(finishes.values(), default=0)}


def resource_request(initial: list, observation: dict) -> dict:
    """Request the ground action/payload check alongside its resource view.

    The ordered premises retain repeated reads. Their physical footprint may
    deduplicate those reads, so checking only that footprint is insufficient.
    """
    return {'schema': 'prime-resource-history-word-v1', 'initialCount': len(initial),
            'costs': 'resource-cost' in observation,
            'word': [{'site': event['occurrence'],
                      'takes': [key for kind, key in event['inputs'] if kind == 'take'],
                      'reads': [key for kind, key in event['inputs'] if kind == 'read'],
                      'outputs': len(event['produced'])}
                     for event in observation['ordered']],
            'graph': [{'event': event['occurrence'], 'parents': event['parents']}
                      for event in observation['causal']['events']],
            'payloads': {
                'profile': PROFILE, 'initial': initial,
                'word': [{'action': event['action'],
                          'inputs': [{'kind': kind, 'key': key, 'value': premise[1]}
                                     for premise, (kind, key) in zip(
                                         event['action'][2][1:], event['inputs'], strict=True)],
                          'produced': [{'key': key, 'value': value}
                                       for key, value in event['produced']]}
                         for event in observation['ordered']],
                'final': [{'key': key, 'value': value}
                          for key, value in observation['final']]}}


def check_resource_projection(checker: Path, initial: list, observation: dict) -> dict:
    """An explicitly selected external checker pays for source checking."""
    try:
        result = subprocess.run([str(checker.resolve())],
                                input=json.dumps(resource_request(initial, observation)) + '\n',
                                capture_output=True, text=True, check=False)
    except OSError as error:
        raise HistoryRefused('ResourceCheckerUnavailable') from error
    if result.returncode:
        raise HistoryRefused('ResourceCheckerFailure')
    try:
        reply = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise HistoryRefused('MalformedResourceCheckerReply') from error
    if not isinstance(reply, dict) or reply.get('schema') != 'prime-resource-history-word-v1' or \
            reply.get('accepted') is not True or reply.get('authority') != 'None':
        raise HistoryRefused('ResourceProjectionRefused')
    if reply.get('sourcePayload') is not True:
        raise HistoryRefused('SourcePayloadUnchecked')
    if 'resource-cost' in observation and reply.get('resourceCost') != observation['resource-cost']:
        raise HistoryRefused('ResourceCostMismatch')
    return reply


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--initial', type=Path, required=True, help='One expression listing initial rows')
    parser.add_argument('--world', type=Path, required=True, help='One SnapshotWorld expression')
    parser.add_argument('--resource-checker', type=Path,
                        help='Optional executable checking the resource-word JSON protocol')
    parser.add_argument('--resource-cost', action='store_true',
                        help='Read source firing count and resource dependency depth')
    args = parser.parse_args()
    try:
        initial, world = parse(args.initial.read_text()), parse(args.world.read_text())
        if len(initial) != 1 or len(world) != 1:
            raise HistoryRefused('ExpectedSingleExpression')
        result = read_history(initial[0], world[0])
        if args.resource_cost:
            result['resource-cost'] = resource_cost(result)
        if args.resource_checker is not None:
            result['resource-check'] = check_resource_projection(args.resource_checker, initial[0], result)
    except (HistoryRefused, ValueError, IndexError) as error:
        status = 'Unknown' if str(error).startswith('Unsupported') else 'Refused'
        print(json.dumps({'profile': PROFILE, 'status': status, 'reason': str(error), 'authority': 'None'}))
        raise SystemExit(1)
    print(json.dumps({'status': 'CheckedData', **result}))


if __name__ == '__main__':
    main()
