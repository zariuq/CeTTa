#!/usr/bin/env python3
"""Compare ground CHR (C0) through the public lang library with SWI-CHR.

Each ordinary source runs twice through (lang:load CHR):
  * query posting: lang:eval over an empty Space, the source query posted;
  * capture posting: the source query is removed and its constraints are
    rows of a fresh Space, in query order; lang:run transforms that Space.
Both final stores are compared as multisets with SWI-Prolog library(chr)
running the original source under the refined profile (optimise off).
The reference runs only in this test; lang calls launch no subprocess.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


def raw(text):
    depth = 1
    while '"' + '#' * depth in text:
        depth += 1
    return 'r' + '#' * depth + '"' + text + '"' + '#' * depth


# ---- Prolog canonical terms (write_canonical subset: atoms, integers, compounds, lists)
def prolog_term(text):
    position = 0

    def skip():
        nonlocal position
        while position < len(text) and text[position] in ' \t\r\n':
            position += 1

    def atom():
        nonlocal position
        # Double-quoted text is an atom name, as under double_quotes=atom.
        if text[position] in "'\"":
            quote = text[position]
            out, position = [], position + 1
            while True:
                c = text[position]
                if c == quote and position + 1 < len(text) and text[position + 1] == quote:
                    out.append(quote); position += 2
                elif c == quote:
                    position += 1; return ''.join(out)
                elif c == '\\':
                    escapes = {'n': '\n', 't': '\t', '\\': '\\', "'": "'", 'r': '\r', 'a': '\a', 'b': '\b', 'f': '\f', 'v': '\v'}
                    out.append(escapes[text[position + 1]]); position += 2
                else:
                    out.append(c); position += 1
        # SWI writes Unicode names unquoted when they start with a letter
        # that is not uppercase; they continue with letters, digits and _.
        start = position
        if position < len(text) and text[position].isalpha() and not text[position].isupper():
            while position < len(text) and (text[position].isalnum() or text[position] == '_'):
                position += 1
            return text[start:position]
        match = re.compile(r'[a-z][A-Za-z0-9_]*|[#$&*+\-./:<=>?@^~\\]+|\[\]').match(text, position)
        if not match:
            raise ValueError('expected an atom at ' + text[position:position + 20])
        position = match.end()
        return match.group(0)

    def term():
        nonlocal position
        skip()
        if text[position] == '[':
            position += 1; skip()
            items = []
            if text[position] == ']':
                position += 1; return ('list', items)
            while True:
                items.append(term()); skip()
                if text[position] == ',':
                    position += 1; continue
                if text[position] == ']':
                    position += 1; return ('list', items)
                raise ValueError('bad list')
        match = re.compile(r'-?[0-9]+').match(text, position)
        if match and (text[position] != '-' or match.end() > position + 1):
            position = match.end(); return ('int', int(match.group(0)))
        name = atom()
        if position < len(text) and text[position] == '(':
            position += 1
            arguments = []
            skip()
            if text[position] == ')':
                position += 1; return ('compound', name, arguments)
            while True:
                arguments.append(term()); skip()
                if text[position] == ',':
                    position += 1; continue
                if text[position] == ')':
                    position += 1; return ('compound', name, arguments)
                raise ValueError('bad compound')
        return ('atom', name)

    result = term(); skip()
    if position != len(text):
        raise ValueError('trailing text in reference store')
    return result


def prolog_value(term):
    if term[0] == 'int':
        return ('int', term[1])
    if term[0] == 'atom':
        return ('atom', term[1])
    if term[0] == 'compound' and term[1] == 'tuple':
        return ('tuple', tuple(prolog_value(a) for a in term[2]))
    raise ValueError('not a C0 value: ' + repr(term))


def prolog_constraint(term):
    if term[0] == 'atom':
        return (term[1],)
    if term[0] == 'compound':
        return (term[1], *map(prolog_value, term[2]))
    raise ValueError('not a constraint: ' + repr(term))


# ---- MeTTa output rows
# The output codec chooses strings for truth names, empty names and NUL;
# input strings and symbols both denote C0 atom names.
def symbol_name(name):
    return bool(name) and '\0' not in name and name not in ('True', 'False')


def string_text(form):
    """The text of a MeTTa string literal, or None for a symbol."""
    if isinstance(form, str) and not isinstance(form, Symbol) and form.startswith('"'):
        return json.loads(form)
    return None


def metta_name(form):
    text = string_text(form)
    if text is not None:
        assert not symbol_name(text), form
        return text
    return str(form)


def metta_value(form):
    if isinstance(form, list):
        return ('tuple', tuple(metta_value(item) for item in form))
    if string_text(form) is None and re.fullmatch(r'-?[0-9]+', form):
        return ('int', int(form))
    return ('atom', metta_name(form))


def metta_constraint(form):
    assert isinstance(form, list) and form, form
    return (metta_name(form[0]), *map(metta_value, form[1:]))


def metta_text(name):
    # Names of letters, digits and _ (Unicode included) are written as
    # symbols, as C0's canonical output writes them; other names as strings.
    if symbol_name(name) and not name[0].isdigit() and all(c.isalnum() or c == '_' for c in name):
        return name
    return json.dumps(name, ensure_ascii=False)


def metta_atom(value):
    kind, data = value
    if kind == 'int':
        return str(data)
    if kind == 'atom':
        return metta_text(data)
    return '(' + ' '.join(map(metta_atom, data)) + ')'


def split_query(source):
    lines = source.splitlines(keepends=True)
    queries = [line for line in lines if line.lstrip().startswith('?-')]
    program = ''.join(line for line in lines if not line.lstrip().startswith('?-'))
    return program, queries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('work', type=Path)
    parser.add_argument('--sources', type=Path, required=True, help='directory of ordinary C0 .chr sources')
    parser.add_argument('--reference', type=Path, required=True, help='chr_source_reference.pl')
    parser.add_argument('--swipl', type=Path, default=Path(shutil.which('swipl') or 'swipl'))
    parser.add_argument('--program', action='append')
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    engine = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(engine / 'tools'))
    from library_spelling import read_forms, Symbol
    globals()['Symbol'] = Symbol
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    swipl = args.swipl.resolve(strict=True)
    sources = sorted(args.sources.resolve(strict=True).glob('*.chr'))
    if args.program:
        sources = [path for path in sources if path.stem in args.program]
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    records = []
    for source in sources:
        name = source.stem
        directory = work / name
        directory.mkdir()
        text = source.read_text()
        reference = subprocess.run([swipl, '-q', str(args.reference.resolve(strict=True)), str(source), str(directory / 'reference.pl')],
                                   capture_output=True, text=True, timeout=120)
        (directory / 'reference.out').write_text(reference.stdout)
        (directory / 'reference.err').write_text(reference.stderr)
        expected = json.loads(reference.stdout)
        if expected['status'] == 'completed':
            expected_store = Counter(prolog_constraint(t) for t in prolog_term(expected['store'])[1])
        program, queries = split_query(text)
        # The query's constraints, in order, as rows of the capture Space.
        rows = []
        for query in queries:
            body = query.strip()[2:].strip().rstrip('.')
            parsed = prolog_term('[' + body + ']')[1]
            rows += [prolog_constraint(t) for t in parsed if t != ('atom', 'true')]
        space_rows = ' '.join(f'($_ (add-atom $space ({" ".join([metta_text(row[0]), *map(metta_atom, row[1:])])})))' for row in rows)
        workload = directory / 'lang.metta'
        workload.write_text(f'''!(import! &self lang)
!(println! (QueryPosting (lang:eval (lang:load CHR) (new-space) {raw(text)})))
!(let* (($space (new-space)) {space_rows}
        ($done (lang:run (lang:load CHR) $space {raw(program)})))
   (println! (CapturePosting $done (collapse (get-atoms $space)))))
''')
        result = subprocess.run([binary, '--lang', 'petta', '--profile', 'extended', workload],
                                capture_output=True, text=True, timeout=600)
        (directory / 'lang.out').write_text(result.stdout)
        (directory / 'lang.err').write_text(result.stderr)
        outputs = {}
        for line in result.stdout.splitlines():
            if line.startswith('(QueryPosting ') or line.startswith('(CapturePosting '):
                form = read_forms(line)[0]
                outputs[str(form[0])] = form[1:]
        def observed(label):
            if label not in outputs:
                return ('missing', None)
            outcome = outputs[label][0]
            if isinstance(outcome, list) and outcome and outcome[0] == 'LangCompleteV1':
                store = outputs[label][1] if label == 'CapturePosting' else outcome[1]
                return ('completed', Counter(metta_constraint(row) for row in store))
            if outcome == 'LangGuestFailureV1':
                return ('failed', None)
            if isinstance(outcome, list) and outcome and outcome[0] == 'LangGuestFaultV1':
                return ('fault', None)
            return ('other', str(outcome))
        checks = {}
        for label in ['QueryPosting', 'CapturePosting']:
            status, store = observed(label)
            if expected['status'] == 'completed':
                checks[label] = status == 'completed' and store == expected_store
            else:
                checks[label] = status == expected['status']
        record = {'program': name, 'reference_status': expected['status'], 'checks': checks,
                  'passed': all(checks.values()) and result.returncode == 0,
                  'source_sha256': digest(source)}
        records.append(record)
        print(('PASS ' if record['passed'] else 'FAIL ') + name + ' ' + json.dumps(checks), flush=True)
    version = subprocess.run([swipl, '--version'], capture_output=True, text=True).stdout.strip()
    summary = {'binary_sha256': digest(binary), 'swipl': version, 'reference_sha256': digest(args.reference.resolve()),
               'programs': records, 'all_passed': all(r['passed'] for r in records),
               'comparison': 'final stores as multisets; query posting and capture posting'}
    (work / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(f"(LangCHRReferenceSummary {sum(r['passed'] for r in records)} of {len(records)} agree)")
    raise SystemExit(0 if summary['all_passed'] else 1)


if __name__ == '__main__':
    main()
