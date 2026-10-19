#!/usr/bin/env python3
"""Project PeTTa's builtin_spec/6 records into the native metadata catalogue."""
import argparse
import json
from pathlib import Path
import subprocess


def payload(value, constructor):
    if not isinstance(value, dict) or value.get('constructor') != constructor:
        raise ValueError(f'expected {constructor}, received {value!r}')
    return value['arguments']


def type_code(value):
    names = {'Number': 'NUMBER', 'Bool': 'BOOL', 'String': 'STRING',
             '%Undefined%': 'UNDEFINED'}
    if isinstance(value, str) and value in names:
        return names[value]
    if isinstance(value, dict) and value.get('variable') in (0, 1):
        return 'VAR' + str(value['variable'])
    if isinstance(value, list) and len(value) == 2 and value[0] == 'List':
        if value[1] == {'variable': 0}:
            return 'LIST_VAR0'
        if value[1] == '%Undefined%':
            return 'LIST_UNDEFINED'
    raise ValueError(f'add an explicit native representation for type {value!r}')


def row_fields(row, ordinal):
    arity = row['arity']
    arity_text = 'PETTA_BUILTIN_VARIADIC' if arity == 'variadic' else str(arity)
    implementation, = payload(row['implementation'], 'implementation')
    evaluation, = payload(row['evaluation'], 'evaluation')
    typing, = payload(row['typing'], 'typing')
    kind, grade, arguments, output, contextual = 'untyped', 'unspecified', [], 'NONE', 'NULL'
    if isinstance(typing, dict) and typing['constructor'] == 'signature':
        grade, arguments, output = payload(typing, 'signature')
        if arity != len(arguments) or len(arguments) > 2:
            raise ValueError(f'extend the signature representation for {row["name"]}/{arity}')
        kind, arguments, output = 'signature', list(map(type_code, arguments)), type_code(output)
    elif isinstance(typing, dict) and typing['constructor'] == 'contextual':
        name, = payload(typing, 'contextual')
        kind, contextual = 'contextual', json.dumps(name)
    elif typing != 'untyped':
        raise ValueError(f'unknown typing record: {typing!r}')
    arguments += ['NONE'] * (2 - len(arguments))
    card, = payload(row['cardinality'], 'cardinality')
    flat = 'unspecified'
    if isinstance(card, str) and card in ('compiler_derived', 'unspecified'):
        rule = card.upper()
    elif isinstance(card, dict) and card['constructor'] == 'fixed':
        flat, = payload(card, 'fixed')
        rule = 'FIXED'
    elif isinstance(card, dict) and card['constructor'] == 'argument_sensitive':
        rule, flat = payload(card, 'argument_sensitive')
        rule = rule.upper()
    elif isinstance(card, dict) and card['constructor'] == 'conditional':
        rule, = payload(card, 'conditional')
        rule = rule.upper()
    else:
        raise ValueError(f'unknown cardinality record: {card!r}')
    lowering, = payload(row['lowering'], 'lowering')
    if lowering == 'generic':
        lowering = 'NULL'
    else:
        name, = payload(lowering, 'special')
        lowering = json.dumps(name)
    return [str(ordinal), json.dumps(row['name']), arity_text, implementation, evaluation,
            kind, grade, *arguments, output, contextual, rule, flat, lowering]


def render(rows, revision):
    keys = [(row['name'], row['arity']) for row in rows]
    if len(keys) != len(set(keys)):
        raise ValueError('duplicate builtin name/arity')
    lines = [f'/* PeTTa builtin language records, checked against upstream {revision[:7]}.',
             ' * Declaration effects and operational cardinality are separate columns. */']
    entries = [(row['name'], 65535 if row['arity'] == 'variadic' else row['arity'], row_fields(row, index))
               for index, row in enumerate(rows)]
    for _, _, fields in sorted(entries):
        lines.append('PETTA_BUILTIN(' + ', '.join(fields) + ')')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream', type=Path, help='PeTTa checkout containing src/typecheck/builtin_registry.pl')
    parser.add_argument('--output', type=Path, default=Path('src/petta_builtin_registry.def'))
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    upstream = args.upstream.resolve()
    revision = subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip()
    exporter = Path(__file__).with_name('export_petta_builtin_registry.pl')
    raw = subprocess.check_output(['swipl', '-q', '-s', str(exporter), '--',
                                   str(upstream/'src/typecheck/builtin_registry.pl')], text=True)
    content = render(json.loads(raw), revision)
    if args.check:
        if args.output.read_text() != content:
            raise SystemExit('builtin catalogue differs from the selected upstream revision')
        print('PASS: builtin catalogue matches upstream ' + revision)
    else:
        args.output.write_text(content)


if __name__ == '__main__':
    main()
