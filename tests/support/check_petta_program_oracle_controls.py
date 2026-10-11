#!/usr/bin/env python3
"""Calibrate the program observer against retained executions and corruptions.

Positive controls use actual model configurations and actual native output.
Negative controls remove or reorder occurrences, change typed values, lose
effects, or omit required residual fields. They check the observation boundary;
they do not establish source semantics or native implementation correctness.
"""

import argparse
import copy
import hashlib
import json
from pathlib import Path

from check_petta_program_oracle import completed_checkpoint_observations, native_observation, structural_atom


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def packed(value):
    return json.loads(json.dumps(value))


def change_answers(model, answers):
    model['answers'] = answers
    for configuration in [model['configuration'], model['checkpoints'][-1]['configuration']]:
        if 'scopes' in configuration:
            configuration['scopes'][-1]['collected'] = answers
        else:
            configuration['control']['answers'] = answers


def world(configuration):
    return configuration['world'] if 'world' in configuration else configuration


def change_effects(model, effects):
    model['effects'] = effects
    world(model['configuration'])['output'] = effects
    world(model['checkpoints'][-1]['configuration'])['output'] = effects


def replace_first_string(atom):
    if atom['kind'] == 'string':
        return {'kind': 'symbol', 'value': atom['value']}, True
    if atom['kind'] == 'expression':
        for index, child in enumerate(atom['items']):
            changed, found = replace_first_string(child)
            if found:
                result = copy.deepcopy(atom)
                result['items'][index] = changed
                return result, True
    return atom, False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--requests', type=Path, required=True)
    parser.add_argument('--native-records', type=Path, required=True)
    parser.add_argument('--configuration-policy', choices=['batch', 'eager-dfs'], default='batch')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    models = [json.loads(line) for line in args.reference.read_text().splitlines() if line.strip()]
    requests = [json.loads(line) for line in args.requests.read_text().splitlines() if line.strip()]
    native = json.loads(args.native_records.read_text())
    if len(models) != len(requests) or not models or not native:
        raise AssertionError('control population is incomplete')
    inputs = [args.reference, args.requests, args.native_records,
              Path(__file__), Path(__file__).with_name('check_petta_program_oracle.py')]
    guards = {str(path.resolve()): digest(path) for path in inputs}
    expected = [packed(completed_checkpoint_observations(model, request, args.configuration_policy))
                for model, request in zip(models, requests)]
    agreeing = {}
    disagreements = []
    for record in native:
        case = record['case']
        stem = args.native_records.parent / (f"{case:04d}-" + record['profile'])
        stdout_path, stderr_path = stem.with_suffix('.stdout'), stem.with_suffix('.stderr')
        guards[str(stdout_path.resolve())] = digest(stdout_path)
        guards[str(stderr_path.resolve())] = digest(stderr_path)
        raw_observation = packed(native_observation(stdout_path.read_text()))
        if raw_observation != record['native_observation']:
            raise AssertionError('retained raw observations changed')
        observation_matches = record['native_observation'] == expected[case]
        actual = (record['exit'] == 0 and not record['timed_out'] and
                  record['parse_error'] is None and not stderr_path.read_bytes() and observation_matches)
        if record['agrees'] is not actual:
            raise AssertionError('retained comparison receipt changed its conclusion')
        if actual:
            agreeing.setdefault(case, record['native_observation'])
        else:
            disagreements.append({'case': case, 'profile': record['profile']})
    results = []

    def observable_corruption(name, case, mutate):
        model = copy.deepcopy(models[case])
        mutate(model)
        try:
            changed = packed(completed_checkpoint_observations(model, requests[case], args.configuration_policy))
        except ValueError:
            results.append({'control': name, 'case': case, 'rejected': True,
                            'kind': 'inconsistent observation history'})
            return
        if changed == agreeing[case]:
            raise AssertionError('observer missed corruption: ' + name)
        results.append({'control': name, 'case': case, 'rejected': True,
                        'kind': 'ordered observation disagreement'})

    def invalid_configuration(name, mutate, case=None):
        if case is None:
            case = next(iter(agreeing))
        model = copy.deepcopy(models[case])
        mutate(model)
        try:
            completed_checkpoint_observations(model, requests[case], args.configuration_policy)
        except (ValueError, KeyError, TypeError):
            results.append({'control': name, 'case': case, 'rejected': True,
                            'kind': 'incomplete or inconsistent replay'})
        else:
            raise AssertionError('observer admitted incomplete replay: ' + name)

    duplicate_case = next(index for index in agreeing
                          if len({json.dumps(atom, sort_keys=True) for atom in models[index]['answers']})
                          < len(models[index]['answers']))
    observable_corruption('drop duplicate occurrence', duplicate_case,
                          lambda model: change_answers(model, model['answers'][1:]))

    def deduplicate(model):
        kept = []
        for atom in model['answers']:
            if atom not in kept:
                kept.append(atom)
        change_answers(model, kept)

    observable_corruption('quotient ordered answers to a set', duplicate_case, deduplicate)
    ordered_case = next(index for index in agreeing if any(
        atom != models[index]['answers'][0] for atom in models[index]['answers'][1:]))

    def reorder_answers(model):
        answers = copy.deepcopy(model['answers'])
        other = next(index for index, atom in enumerate(answers) if atom != answers[0])
        answers[0], answers[other] = answers[other], answers[0]
        change_answers(model, answers)

    observable_corruption('reorder answer alternatives', ordered_case, reorder_answers)
    string_case = next(index for index in agreeing if any(
        replace_first_string(atom)[1] for atom in models[index]['answers']))

    def change_string(model):
        answers = copy.deepcopy(model['answers'])
        for index, atom in enumerate(answers):
            changed, found = replace_first_string(atom)
            if found:
                answers[index] = changed
                change_answers(model, answers)
                return

    observable_corruption('replace a string with the same-spelled symbol', string_case, change_string)
    effect_case = next(index for index in agreeing if models[index]['effects'])
    observable_corruption('erase an already performed effect', effect_case,
                          lambda model: change_effects(model, model['effects'][1:]))
    effect_order_case = next(index for index in agreeing if any(
        atom != models[index]['effects'][0] for atom in models[index]['effects'][1:]))

    def reorder_effects(model):
        effects = copy.deepcopy(model['effects'])
        other = next(index for index, atom in enumerate(effects) if atom != effects[0])
        effects[0], effects[other] = effects[other], effects[0]
        change_effects(model, effects)

    observable_corruption('reorder performed effects', effect_order_case, reorder_effects)
    invalid_configuration('completion changed to exhaustion', lambda model: model.update(status='exhausted'))
    invalid_configuration('split agreement lost', lambda model: model.update(split_equal=False))
    invalid_configuration('checkpoint dropped', lambda model: model['checkpoints'].pop(0))
    invalid_configuration('fuel total changed', lambda model: model.update(fuel_total=model['fuel_total'] + 1))
    invalid_configuration('store field omitted', lambda model: world(model['configuration'])['state'].pop('absent_cells'))
    invalid_configuration('private-space tail lost', lambda model: world(model['configuration'])['state'].update(unallocated_spaces=[[]]))
    invalid_configuration('undeclared named cell admitted', lambda model: world(model['configuration'])['state'].update(cells=[['unexpected', {}]]))
    invalid_configuration('read-only store changed mid-run', lambda model: world(model['checkpoints'][0]['configuration'])['state'].update(next=999))
    if args.configuration_policy == 'batch':
        invalid_configuration('unfinished frame at completion', lambda model: model['configuration']['frames'].append({'kind': 'unfinished'}))
        invalid_configuration('terminal control omitted', lambda model: model['configuration'].pop('control'))
    else:
        invalid_configuration('unfinished branch at completion', lambda model: model['configuration']['scopes'][0]['frontier'].append(
            {'control': {'kind': 'returned', 'value': {'kind': 'symbol', 'value': 'pending'}}, 'frames': []}))
        invalid_configuration('root scope omitted', lambda model: model['configuration']['scopes'].clear())
        invalid_configuration('reference policy changed', lambda model: model.update(policy='batch'))
        invalid_configuration('global world omitted', lambda model: model['configuration'].pop('world'))
        invalid_configuration('root scope gained a caller', lambda model: model['configuration']['scopes'][0].update(caller=[]))
        invalid_configuration('collected occurrences field omitted', lambda model: model['configuration']['scopes'][0].pop('collected'))
        invalid_configuration('frontier field omitted', lambda model: model['configuration']['scopes'][0].pop('frontier'))
        invalid_configuration('caller field omitted', lambda model: model['configuration']['scopes'][0].pop('caller'))

        def find_checkpoint(test):
            for case in agreeing:
                for index, checkpoint in enumerate(models[case]['checkpoints']):
                    if test(checkpoint['configuration']):
                        return case, index
            raise AssertionError('actual checkpoint population lacks the required control')

        nested_case, nested_index = find_checkpoint(lambda configuration: len(configuration['scopes']) > 1)
        invalid_configuration('collection scope lost its caller', lambda model:
                              model['checkpoints'][nested_index]['configuration']['scopes'][0].update(caller=None), nested_case)
        branch_case, branch_index = find_checkpoint(lambda configuration: bool(configuration['scopes'][0]['frontier']))
        invalid_configuration('saved branch frames omitted', lambda model:
                              model['checkpoints'][branch_index]['configuration']['scopes'][0]['frontier'][0].pop('frames'), branch_case)
        bound_case, bound_index = find_checkpoint(lambda configuration:
            any(branch['control']['kind'] == 'evaluate' for branch in configuration['scopes'][0]['frontier']))

        def omit_bindings(model):
            branches = model['checkpoints'][bound_index]['configuration']['scopes'][0]['frontier']
            next(branch for branch in branches if branch['control']['kind'] == 'evaluate')['control'].pop('bindings')

        invalid_configuration('saved branch bindings omitted', omit_bindings, bound_case)
        argument_case, argument_index = find_checkpoint(lambda configuration:
            any(frame['kind'] == 'argument' for branch in configuration['scopes'][0]['frontier'] for frame in branch['frames']))

        def omit_pending_argument(model):
            branches = model['checkpoints'][argument_index]['configuration']['scopes'][0]['frontier']
            next(frame for branch in branches for frame in branch['frames'] if frame['kind'] == 'argument').pop('remaining')

        invalid_configuration('caller pending argument omitted', omit_pending_argument, argument_case)
    invalid_configuration('input remainder lost', lambda model: world(model['configuration']).update(input=['unexpected']))
    invalid_configuration('output history lost', lambda model: world(model['configuration']).update(output=['unexpected']))
    scalar_negatives = [
        {'kind': 'number', 'value': True}, {'kind': 'number', 'value': '1.5'},
        {'kind': 'boolean', 'value': 'true'}, {'kind': 'string', 'value': 1},
        {'kind': 'symbol', 'value': 'a', 'extra': 1}, {'kind': 'unknown', 'value': 'a'},
    ]
    for atom in scalar_negatives:
        try:
            structural_atom(atom)
        except ValueError:
            results.append({'control': 'malformed typed atom', 'atom': atom, 'rejected': True})
        else:
            raise AssertionError('malformed typed atom was admitted')
    changed = [name for name, value in guards.items() if digest(Path(name)) != value]
    if changed:
        raise AssertionError('control inputs changed')
    receipt = {'model_configurations': len(models), 'native_records': len(native),
               'configuration_policy': args.configuration_policy,
               'agreeing_native_records': len(native) - len(disagreements),
               'retained_native_disagreements': disagreements, 'negative_controls': results,
               'source_identities': guards, 'changed_inputs': changed,
               'scope': 'Calibration of ordered observations and replay schema; native/model disagreements remain failures of the program comparison.'}
    with args.out.open('x') as destination:
        destination.write(json.dumps(receipt, indent=2) + '\n')
    print('PASS observer calibration:', len(models), 'configurations,', len(results),
          'corruptions rejected; retained native disagreements:', len(disagreements))


if __name__ == '__main__':
    main()
