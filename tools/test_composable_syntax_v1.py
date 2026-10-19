#!/usr/bin/env python3
"""Public syntax-value lifecycle and refusal controls, without private calls."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
POLICY = '(SyntaxLexicalPolicyV1 ScalarScannerlessAllV1)'


def text(value):
    return json.dumps(value, ensure_ascii=False)


def form(head, *args):
    return '(' + ' '.join((head, *args)) + ')'


def cons_list(values):
    result = 'LNil'
    for value in reversed(values):
        result = form('LCons', value, result)
    return result


def rule(label, literal, parameters=(), syntax=None):
    params = [form('TermSimple', text(name), form('TBase', text(sort)))
              for name, sort in parameters]
    tokens = syntax if syntax is not None else [form('SyntaxTerminal', text(literal))]
    return form('GrammarRule', text(label), text('E'), cons_list(params),
                cons_list(tokens), 'EvalNone')


def origin(namespace, site):
    return form('SyntaxOriginV1', text(namespace), str(site))


def parts(name, rules, sites=None, categories=None, policy=POLICY):
    categories = categories or [form('TypeDecl', text('E'), 'CarrierAst')]
    language = form('GSLTLanguageDefWireV1', text(name), cons_list(categories),
                    cons_list(rules), 'LNil', 'LNil')
    profile = form('GSLTParserProfileLayerV1', text(name), text('E'), 'LNil', 'LNil')
    sites = sites or [origin(name, index) for index in range(len(rules))]
    origins = form('SyntaxOriginsV1', cons_list([origin('shared-categories', i)
                   for i in range(len(categories))]), cons_list(sites), 'LNil', 'LNil')
    return language, profile, policy, origins


def operand(name, rules, **options):
    return form('langdef:syntax-gslt', *parts(name, rules, **options))


def lets(bindings, body):
    for name, value in reversed(bindings):
        body = form('let', name, value, body)
    return body


def check_case(name, expression, pattern):
    return form('case', expression,
                form(form(pattern, form('SyntaxCheckV1', name, 'yes')),
                     form('$other', form('SyntaxCheckV1', name, 'no', '$other'))))


def refusal(name, expression, reason):
    return check_case(name, expression, form('SyntaxGSLTRefusedV1', reason, '$witness'))


def parser_check(name, value, source, count, evidence=True, expected_values=None):
    values = expected_values or '(' + ' '.join('$v' + str(i) for i in range(count)) + ')'
    if evidence:
        parse = form('langdef:parse-authored-text-evidence', '$parser', text(source), '2000')
        pattern = form('LangDef:AuthoredParseEvidenceAccepted', '$name', '$binding',
                       '$input', '$digest', form('pf-v1', '$entry', '$extent', '$roots', '$nodes', '$choices'),
                       values)
    else:
        parse = form('langdef:parse-authored-text', '$parser', text(source))
        pattern = form('LangDef:AuthoredParseAccepted', '$name', '$binding', '$input',
                       values)
    return lets([('$parser', form('langdef:load-syntax-gslt-parser', value)),
                 ('$answer', parse),
                 ('$_closed', form('langdef:close-authored-parser', '$parser'))],
                check_case(name, '$answer', pattern))


def program():
    x = rule('X', 'x')
    y = rule('Y', 'y')
    pair = rule('Pair', '', [('a', 'E'), ('b', 'E')], [
        form('SyntaxTerminal', text('(')), form('SyntaxNonTerminal', text('a')),
        form('SyntaxTerminal', text(',')), form('SyntaxNonTerminal', text('b')),
        form('SyntaxTerminal', text(')'))])
    left = operand('Left', [x, pair])
    right = operand('Right', [y])
    join = form('langdef:join-syntax-gslts', text('Combined'), text('E'), '$left', '$right')
    inputs = [('$left', left), ('$right', right), ('$joined', join)]
    queries = []

    def add(name, expression):
        queries.append('!' + expression)

    add('admission', lets([('$left', left)], check_case('admission', '$left',
        form('SyntaxGSLTAdmittedV1', '$value'))))
    add('inspection', lets(inputs, check_case('inspection',
        form('langdef:inspect-syntax-gslt', '$joined'),
        form('SyntaxGSLTInspectionV1', '$language', '$profile', POLICY, '$origins'))))
    add('combined', lets(inputs, parser_check('combined', '$joined', '(x,y)', 1)))
    for name, value in [('left-alone', '$left'), ('right-alone', '$right')]:
        add(name, lets(inputs, lets([
            ('$parser', form('langdef:load-syntax-gslt-parser', value)),
            ('$answer', form('langdef:parse-authored-text-evidence', '$parser', text('(x,y)'), '2000')),
            ('$_closed', form('langdef:close-authored-parser', '$parser'))],
            check_case(name, '$answer', form('LangDef:AuthoredParseEvidenceRejected',
                       '$n', '$b', '$s', '$furthest', '$expected', '$d', '$f')))))
    add('shared-ancestor', lets(inputs + [('$again', form('langdef:join-syntax-gslts',
        text('Combined'), text('E'), '$joined', '$left'))],
        check_case('shared-ancestor', form('==', '$again', '$joined'), 'True')))
    add('coherent', lets([('$left', left), ('$right', right)], check_case('coherent',
        form('langdef:join-coherent-syntax-gslts', text('Combined'), text('E'), '$left', '$right'),
        form('SyntaxGSLTAdmittedV1', '$value'))))

    # One label with different bodies/signatures, or an independent origin,
    # must be refused instead of becoming first-wins or body-only deduplication.
    variants = [
        ('label-conflict', operand('Other', [rule('X', 'z')])),
        ('equal-body-distinct-origin', operand('Other', [x])),
        ('signature-conflict', operand('Other', [rule('X', '', [('a', 'E')],
            [form('SyntaxNonTerminal', text('a'))])])),
    ]
    for name, other in variants:
        add(name, lets([('$left', left), ('$other', other)], refusal(name,
            form('langdef:join-syntax-gslts', text('Conflict'), text('E'), '$left', '$other'),
            'label-conflict')))
    renamed = operand('Renamed', [rule('RenamedX', 'z')], sites=[origin('Left', 0)])
    add('origin-view', lets([('$left', left), ('$renamed', renamed),
        ('$view', form('langdef:join-syntax-gslts', text('View'), text('E'), '$left', '$renamed'))],
        parser_check('origin-view', '$view', 'z', 1)))
    add('origin-coherent-refusal', lets([('$left', left), ('$renamed', renamed)], refusal(
        'origin-coherent-refusal', form('langdef:join-coherent-syntax-gslts', text('Coherent'),
        text('E'), '$left', '$renamed'), 'origin-conflict')))
    add('missing-reference', refusal('missing-reference', operand('Missing', [
        rule('Missing', '', [('a', 'Undeclared')], [form('SyntaxNonTerminal', text('a'))])]),
        'grammar-admission'))
    add('missing-start', lets(inputs, refusal('missing-start', form('langdef:join-syntax-gslts',
        text('BadStart'), text('Undeclared'), '$left', '$right'), 'grammar-admission')))
    add('origin-list-length', refusal('origin-list-length', form('langdef:syntax-gslt',
        *parts('WrongOrigins', [x])[:3], '(SyntaxOriginsV1 LNil LNil LNil LNil)'),
        'origin-occurrence-mismatch'))
    malformed = form('SyntaxGSLTAdmittedV1', form('SyntaxGSLTV1',
        *parts('OtherPolicy', [y], policy='(SyntaxLexicalPolicyV1 MaximalMunchV1)')))
    add('policy-mismatch', lets([('$left', left)], refusal('policy-mismatch',
        form('langdef:join-syntax-gslts', text('BadPolicy'), text('E'), '$left', malformed),
        'policy-mismatch')))
    add('unsupported-policy', refusal('unsupported-policy', operand('OtherPolicy', [x],
        policy='(SyntaxLexicalPolicyV1 MaximalMunchV1)'), 'unsupported-policy'))
    forged = form('SyntaxGSLTAdmittedV1', form('SyntaxGSLTV1',
        *parts('Missing', [rule('Missing', '', [('a', 'Undeclared')],
            [form('SyntaxNonTerminal', text('a'))])])))
    add('forged-observation', check_case('forged-observation',
        form('langdef:inspect-syntax-gslt', forged),
        '(SyntaxGSLTInspectionV1 $language $profile $policy $origins)'))
    add('forged-admission', refusal('forged-admission',
        form('langdef:inspect-syntax-gslt-checked', forged), 'grammar-admission'))
    add('forged-compilation', refusal('forged-compilation',
        form('langdef:load-syntax-gslt-parser', forged), 'grammar-admission'))
    # A failed join must not disturb an already compiled parser.
    add('atomic-refusal', lets(inputs + [
        ('$parser', form('langdef:load-syntax-gslt-parser', '$joined')),
        ('$_refused', form('langdef:join-syntax-gslts', text('Bad'), text('Missing'), '$left', '$right')),
        ('$answer', form('langdef:parse-authored-text', '$parser', text('(x,y)'))),
        ('$_closed', form('langdef:close-authored-parser', '$parser'))],
        check_case('atomic-refusal', '$answer', form('LangDef:AuthoredParseAccepted',
            '$n', '$b', '$s', '($tree)'))))

    # Ambiguity is retained by the packed forest; the public evidence entry
    # independently checks both GLL and GLR, not merely a chosen tree.
    amb_left = operand('AmbLeft', [rule('A', 'z')])
    amb_right = operand('AmbRight', [rule('B', 'z')])
    add('ambiguity', lets([('$a', amb_left), ('$b', amb_right),
        ('$amb', form('langdef:join-syntax-gslts', text('Ambiguous'), text('E'), '$a', '$b'))],
        parser_check('ambiguity', '$amb', 'z', 2)))
    add('multiplicity', lets([('$a', amb_left), ('$b', amb_right),
        ('$amb', form('langdef:join-syntax-gslts', text('Ambiguous'), text('E'), '$a', '$b')),
        ('$again', form('langdef:join-syntax-gslts', text('Ambiguous'), text('E'), '$amb', '$amb'))],
        parser_check('multiplicity', '$again', 'z', 2)))
    # Equal nullary child values at different sites remain two occurrences.
    epsilon = rule('Empty', '', syntax=[])
    two = rule('Two', '()', [('a', 'E'), ('b', 'E')], [
        form('SyntaxTerminal', text('(')), form('SyntaxNonTerminal', text('a')),
        form('SyntaxNonTerminal', text('b')), form('SyntaxTerminal', text(')'))])
    add('equal-valued-occurrences', lets([('$eps', operand('EmptySites', [epsilon, two]))],
        parser_check('equal-valued-occurrences', '$eps', '()', 1,
            expected_values='((CstRuleV1 "Two" 0 2 (CstRuleV1 "Empty" 1 1) (CstRuleV1 "Empty" 1 1)))')))

    gt_pair = rule('TwoGT', '', [('a', 'E'), ('b', 'E')],
                   [form('SyntaxNonTerminal', text('a')), form('SyntaxNonTerminal', text('b'))])
    add('gt-before', lets([('$gt', operand('GT', [rule('GT', '>'), gt_pair]))],
        parser_check('gt-before', '$gt', '>>', 1)))
    add('gt-after', lets([('$gt', operand('GT', [rule('GT', '>'), gt_pair])),
        ('$shift', operand('Shift', [rule('SHR', '>>')])),
        ('$both', form('langdef:join-syntax-gslts', text('Operators'), text('E'), '$gt', '$shift'))],
        parser_check('gt-after', '$both', '>>', 2)))
    # Data rejection must never evaluate a constructor-shaped effect.
    payload = '(Payload (println! "SYNTAX-UNEXPECTED-EFFECT"))'
    add('inspect-no-effects', refusal('inspect-no-effects',
        form('langdef:inspect-syntax-gslt', payload), 'unadmitted-value'))
    add('admit-no-effects', refusal('admit-no-effects',
        form('langdef:syntax-gslt', payload, payload, POLICY, payload), 'malformed-envelope'))
    add('join-no-effects', lets([('$left', left)], refusal('join-no-effects',
        form('langdef:join-syntax-gslts', text('Never'), text('E'), '$left', payload),
        'unadmitted-value')))
    add('load-no-effects', refusal('load-no-effects',
        form('langdef:load-syntax-gslt-parser', payload), 'unadmitted-value'))
    # Even an otherwise valid declaration whose constructor has a program
    # equation remains syntax data during admission and compilation.
    queries.append('(= (TypeDecl $name $kind) (println! "SYNTAX-UNEXPECTED-EFFECT"))')
    add('admitted-constructor-data', lets([('$left', left)], check_case(
        'admitted-constructor-data', form('langdef:inspect-syntax-gslt', '$left'),
        form('SyntaxGSLTInspectionV1', '$language', '$profile', POLICY, '$origins'))))
    add('compiled-constructor-data', lets(inputs, parser_check(
        'compiled-constructor-data', '$joined', '(x,y)', 1)))
    count = sum(query.startswith('!') for query in queries)
    return '!(import! &self langdef)\n' + '\n'.join(queries) + '\n', count


def run(binary, scratch):
    source, count = program()
    path = scratch / 'composable-syntax-client.metta'
    path.write_text(source)
    for language, profile in [('prime', None), ('he', 'extended'), ('petta', 'extended')]:
        command = [str(binary), '--lang', language]
        if profile:
            command += ['--profile', profile]
        result = subprocess.run(command + [str(path)], cwd=ROOT, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        (scratch / (language + '.stdout')).write_text(result.stdout)
        (scratch / (language + '.stderr')).write_text(result.stderr)
        rows = [line for line in result.stdout.splitlines() if 'SyntaxCheckV1' in line]
        bad = [line for line in rows if ' yes)' not in line]
        if result.returncode or result.stderr or len(rows) != count or bad or \
                'SYNTAX-UNEXPECTED-EFFECT' in result.stdout:
            raise AssertionError(f'{language}: {len(rows)}/{count} rows, {[row[:240] for row in bad]}\n{result.stderr}')
        print(f'(ComposableSyntaxV1Summary {language} {count} 0)')
    # Independent selection counterexample, not a new runtime lexer and not
    # an adequacy proof: every old terminal span survives set union, but
    # maximal munch can select a different segmentation of the same text.
    old = {(0, 1, 'GT'), (1, 2, 'GT')}
    joined = old | {(0, 2, 'SHR')}
    assert old <= joined
    choose = lambda spans, start: max((s for s in spans if s[0] == start), key=lambda s: s[1])
    assert choose(old, 0) != choose(joined, 0)
    print('(SyntaxSelectionCounterexampleV1 GT SHR raw-spans-preserved selected-spans-changed)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--scratch', type=Path)
    args = parser.parse_args()
    if args.scratch:
        args.scratch.mkdir(parents=True, exist_ok=True)
        run(args.cetta.resolve(), args.scratch)
    else:
        base = Path(os.environ.get('CETTA_TEST_TMPDIR', ROOT / 'tmp'))
        base.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='composable-syntax-v1-', dir=base) as directory:
            run(args.cetta.resolve(), Path(directory))
