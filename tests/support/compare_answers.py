#!/usr/bin/env python3
"""Compare a program's output with its expected output as observations.

A query's answers are a bag: they agree up to their order, never up to the
order of anything inside an answer.  The answer of a query whose form is a
`collapse` is a list in no promised order, so its elements agree as a bag.
Output a program writes while it runs (effects) is compared in order.

PeTTa prints each answer on its own line, after every query has run, so the
lines alone do not say which query an answer belongs to.  This tool runs the
program once more with a marker query before every top-level `!` form and one
at the end; the lines between two markers are one query's answers, and the
counts of those answers split the expected output, which needs no markers of
its own.  HE prints each query's answers on one bracketed line, which frames
them already.

usage: compare_answers.py --lang petta|he --program P --expected E \
           --actual A [--cetta BIN [ARG...]]
  --cetta is required for petta: the command that ran the program.
exit status 0 when the outputs agree, 1 otherwise."""
import argparse
import collections
import os
import subprocess
import sys
import tempfile
from pathlib import Path

MARKER = '(__cetta_answers_frame__)'
FRAMED_TIMEOUT = 120


def top_level_forms(text):
    """The offsets and texts of the top-level forms: a form starts at a
    token at depth 0, and `!` binds to the form after it."""
    forms, depth, i, start, bang = [], 0, 0, None, None
    n = len(text)
    while i < n:
        c = text[i]
        if c == ';':
            while i < n and text[i] != '\n':
                i += 1
            continue
        if c == '"':
            if depth == 0 and start is None:
                start = i
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == '\\' else 1
            i += 1
            if depth == 0:
                forms.append((bang if bang is not None else start, text[start:i]))
                start = bang = None
            continue
        if c.isspace():
            if depth == 0 and start is not None and text[start] != '(':
                forms.append((bang if bang is not None else start, text[start:i]))
                start = bang = None
            i += 1
            continue
        if depth == 0 and c == '!' and start is None:
            bang = i
            i += 1
            continue
        if depth == 0 and start is None:
            start = i
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                forms.append((bang if bang is not None else start, text[start:i + 1]))
                start = bang = None
        i += 1
    if start is not None:
        forms.append((bang if bang is not None else start, text[start:]))
    return [(at, form, text[at] == '!') for at, form in forms]


def is_collapse(form):
    body = form.lstrip('!').strip()
    return body.startswith('(collapse') and body[len('(collapse'):][:1] in (' ', '\t', '\n', ')')


def framed(text):
    """The program with a marker query before every top-level query and at
    the end, and whether each query's form is a collapse."""
    out, last, kinds = [], 0, []
    for at, form, query in top_level_forms(text):
        if not query:
            continue
        out.append(text[last:at])
        out.append(f'!{MARKER}\n')
        last = at
        kinds.append(is_collapse(form))
    out.append(text[last:])
    out.append(f'\n!{MARKER}\n')
    return ''.join(out), kinds


def split_elements(body):
    """The top-level elements of a printed list or bracket body."""
    elements, depth, start, i, quoted = [], 0, None, 0, False
    while i < len(body):
        c = body[i]
        if quoted:
            if c == '\\':
                i += 1
            elif c == '"':
                quoted = False
        elif c == '"':
            quoted = True
            if depth == 0 and start is None:
                start = i
        elif c in '([{':
            if depth == 0 and start is None:
                start = i
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif depth == 0 and (c.isspace() or c == ','):
            if start is not None:
                elements.append(body[start:i])
                start = None
        elif start is None:
            start = i
        i += 1
    if start is not None:
        elements.append(body[start:])
    return [e.strip() for e in elements if e.strip()]


def collapse_bag(line):
    """A collapse's printed list as the bag of its elements."""
    line = line.strip()
    if line.startswith('(') and line.endswith(')'):
        return collections.Counter(split_elements(line[1:-1]))
    return collections.Counter([line])


def query_bag(lines, collapse):
    if collapse and len(lines) == 1:
        return ('collapse', collapse_bag(lines[0]))
    return ('answers', collections.Counter(lines))


def compare_petta(program, expected, actual, cetta):
    # The framed copy runs beside the program, so a relative command must
    # be resolved from where this tool was called.
    if os.sep in cetta[0] and os.path.exists(cetta[0]):
        cetta = [os.path.abspath(cetta[0]), *cetta[1:]]
    text = Path(program).read_text()
    source, kinds = framed(text)
    with tempfile.NamedTemporaryFile('w', suffix='.metta', delete=False,
                                     dir=os.path.dirname(os.path.abspath(program))) as f:
        f.write(source)
        framed_path = f.name
    # The framed copy sits beside the program, so its own timeout, shorter
    # than a caller's, still removes it.
    try:
        run = subprocess.run([*cetta, framed_path], capture_output=True, text=True,
                             cwd=os.path.dirname(os.path.abspath(program)),
                             timeout=FRAMED_TIMEOUT)
    except subprocess.TimeoutExpired:
        return False, f'the framed run took over {FRAMED_TIMEOUT} s'
    finally:
        os.unlink(framed_path)
    lines = run.stdout.splitlines()
    marks = [i for i, line in enumerate(lines) if line.strip() == MARKER]
    if len(marks) != len(kinds) + 1:
        return False, f'framing failed: {len(marks)} markers for {len(kinds)} queries'
    counts = [marks[k + 1] - marks[k] - 1 for k in range(len(kinds))]
    effects = marks[0]
    want, got = expected.splitlines(), actual.splitlines()
    if len(want) != effects + sum(counts) or len(got) != len(want):
        return False, f'expected {len(want)} lines, got {len(got)}; the framed run gives {effects} effects and {sum(counts)} answers'
    if want[:effects] != got[:effects]:
        return False, 'effects differ'
    at = effects
    for k, count in enumerate(counts):
        if query_bag(want[at:at + count], kinds[k]) != query_bag(got[at:at + count], kinds[k]):
            return False, f'query {k + 1} answers differ as a bag'
        at += count
    return True, ''


def compare_he(program, expected, actual):
    forms = [form for _, form, query in top_level_forms(Path(program).read_text()) if query]
    kinds = [is_collapse(form) for form in forms]
    want, got = expected.splitlines(), actual.splitlines()
    if len(want) != len(got):
        return False, f'expected {len(want)} lines, got {len(got)}'
    query = 0
    for a, b in zip(want, got):
        bracketed = a.startswith('[') and a.endswith(']') and b.startswith('[') and b.endswith(']')
        if not bracketed:
            if a != b:
                return False, f'line differs: {a!r} / {b!r}'
            continue
        collapse = query < len(kinds) and kinds[query]
        query += 1
        ea, eb = split_elements(a[1:-1]), split_elements(b[1:-1])
        if collapse and len(ea) == 1 and len(eb) == 1:
            if collapse_bag(ea[0]) != collapse_bag(eb[0]):
                return False, f'collapse differs as a bag: {a!r} / {b!r}'
        elif collections.Counter(ea) != collections.Counter(eb):
            return False, f'answers differ as a bag: {a!r} / {b!r}'
    return True, ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lang', choices=('petta', 'he'), required=True)
    ap.add_argument('--program', required=True)
    ap.add_argument('--expected', required=True)
    ap.add_argument('--actual', required=True)
    ap.add_argument('--cetta', nargs=argparse.REMAINDER)
    a = ap.parse_args()
    expected = Path(a.expected).read_text()
    actual = Path(a.actual).read_text()
    if expected.rstrip('\n') == actual.rstrip('\n'):
        return 0
    if a.lang == 'petta':
        if not a.cetta:
            print('compare_answers: --cetta is required for petta', file=sys.stderr)
            return 2
        ok, why = compare_petta(a.program, expected, actual, a.cetta)
    else:
        ok, why = compare_he(a.program, expected, actual)
    if not ok:
        print(f'compare_answers: {why}', file=sys.stderr)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
