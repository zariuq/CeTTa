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
import json
import os
import re
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


def framed(text, marker=MARKER):
    """The program with a marker query before every top-level query and at
    the end, and whether each query's form is a collapse."""
    out, last, kinds = [], 0, []
    for at, form, query in top_level_forms(text):
        if not query:
            continue
        out.append(text[last:at])
        out.append(f'!{marker}\n')
        last = at
        kinds.append(is_collapse(form))
    out.append(text[last:])
    out.append(f'\n!{marker}\n')
    return ''.join(out), kinds


def representation_queries(text):
    """Only a written top-level repr/repra grants text-as-term provenance."""
    return [bool(re.match(r'^\((repr|repra)\s', form.strip()))
            for _, form, query in top_level_forms(text) if query]


def split_elements(body, petta=False):
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
        elif c in ('(' if petta else '([{'):
            if depth == 0 and start is None:
                start = i
            depth += 1
        elif c in (')' if petta else ')]}'):
            depth -= 1
        elif depth == 0 and (c.isspace() or (not petta and c == ',')):
            if start is not None:
                elements.append(body[start:i])
                start = None
        elif start is None:
            start = i
        i += 1
    if start is not None:
        elements.append(body[start:])
    return [e.strip() for e in elements if e.strip()]


def collapse_bag(line, petta=False):
    """A collapse's printed list as the bag of its elements."""
    line = line.strip()
    if line.startswith('(') and line.endswith(')'):
        return collections.Counter(split_elements(line[1:-1], petta=petta))
    return collections.Counter([line])


def query_bag(lines, collapse):
    if collapse and len(lines) == 1:
        return ('collapse', collapse_bag(lines[0], petta=True))
    return ('answers', collections.Counter(lines))


TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+')


def alpha_tokens(value):
    """Rename variables in one answer, retaining aliasing and literal strings."""
    names = {}
    return tuple(('variable', names.setdefault(token, len(names)))
                 if token.startswith('$') else ('literal', token)
                 for token in TOKEN.findall(value))


def collapse_equal(left, right):
    """Compare an unordered collection while retaining sharing across elements.

    Normalizing every element separately loses the distinction between ($x $x)
    and ($x $y). Keep one bijection for the complete collection instead.
    """
    if not (left.startswith('(') and left.endswith(')') and
            right.startswith('(') and right.endswith(')')):
        return alpha_tokens(left) == alpha_tokens(right)
    lhs, rhs = split_elements(left[1:-1], petta=True), split_elements(right[1:-1], petta=True)
    if len(lhs) != len(rhs):
        return False
    if collections.Counter(lhs) == collections.Counter(rhs):
        return True
    groups = collections.defaultdict(list)
    for index, value in enumerate(rhs):
        groups[alpha_tokens(value)].append(index)
    if collections.Counter(map(alpha_tokens, lhs)) != collections.Counter(map(alpha_tokens, rhs)):
        return False
    # Ground collections use the fast path above. For open collections the
    # most selective shapes first avoid permutations of unrelated elements.
    lhs.sort(key=lambda value: len(groups[alpha_tokens(value)]))
    rhs_tokens = [TOKEN.findall(value) for value in rhs]

    def match(at, used, forward, backward):
        if at == len(lhs):
            return True
        tokens = TOKEN.findall(lhs[at])
        for index in groups[alpha_tokens(lhs[at])]:
            if index in used:
                continue
            new_forward, new_backward = dict(forward), dict(backward)
            valid = True
            for a, b in zip(tokens, rhs_tokens[index]):
                if a.startswith('$') and b.startswith('$'):
                    if new_forward.setdefault(a, b) != b or new_backward.setdefault(b, a) != a:
                        valid = False
                        break
                elif a != b:
                    valid = False
                    break
            if valid and match(at + 1, used | {index}, new_forward, new_backward):
                return True
        return False

    return match(0, set(), {}, {})


def representation_key(value):
    try:
        decoded = json.loads(value)
    except (ValueError, TypeError):
        decoded = value
    return alpha_tokens(decoded if isinstance(decoded, str) else value)


def answers_equal(left, right, collapse=False, representation=False):
    if len(left) != len(right):
        return False
    if not collapse:
        key = representation_key if representation else alpha_tokens
        return collections.Counter(map(key, left)) == collections.Counter(map(key, right))
    remaining = list(right)
    for answer in left:
        for index, candidate in enumerate(remaining):
            if collapse_equal(answer, candidate):
                remaining.pop(index)
                break
        else:
            return False
    return True


def observe_framed(stdout, kinds, marker=MARKER):
    """Decode this implementation's own query boundaries; never borrow counts.

    Both runners buffer query answers until after effects. An absent final
    marker or trailing text is unfinished/ambiguous execution, not agreement.
    """
    lines = stdout.splitlines()
    marks = [i for i, line in enumerate(lines) if line.strip() == marker]
    if len(marks) != len(kinds) + 1 or marks[-1] != len(lines) - 1:
        raise ValueError(f'incomplete framing: {len(marks)} markers for {len(kinds)} queries')
    return dict(effects=lines[:marks[0]],
                queries=[lines[marks[k] + 1:marks[k + 1]] for k in range(len(kinds))])


def compare_framed(kinds, expected, actual, representations=None):
    """Compare independently decoded observations, including each query bag."""
    if expected['effects'] != actual['effects']:
        return False, 'effects differ'
    if len(expected['queries']) != len(kinds) or len(actual['queries']) != len(kinds):
        return False, 'query count differs'
    for index, collapse in enumerate(kinds):
        representation = representations is not None and representations[index]
        if not answers_equal(expected['queries'][index], actual['queries'][index], collapse, representation):
            return False, f'query {index + 1} answers differ'
    return True, ''


def compare_completed_runs(kinds, expected, actual, representations=None):
    """Agreement of completed runs; equal crash output is never a success."""
    for record in (expected, actual):
        if record['returncode'] != 0 or record.get('timed_out') or 'observations' not in record:
            return False, 'execution incomplete or raised; inspect each outcome'
    if expected.get('stderr', '') != actual.get('stderr', ''):
        return False, 'stderr observations differ'
    return compare_framed(kinds, expected['observations'], actual['observations'], representations)


def compare_petta(program, expected, actual, cetta):
    # Keep the invocation's working directory, including for wrapper commands
    # with a relative binary argument. The copy's location beside the source
    # preserves source-relative imports without changing the process context.
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
                             timeout=FRAMED_TIMEOUT)
    except subprocess.TimeoutExpired:
        return False, f'the framed run took over {FRAMED_TIMEOUT} s'
    finally:
        os.unlink(framed_path)
    if run.returncode != 0 or run.stderr:
        return False, f'framed execution failed (exit {run.returncode}): {run.stderr.strip()}'
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
