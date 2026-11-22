#!/usr/bin/env python3
"""Two-way matching over stored rows with variables, against a reference model.

Matching is two-way: a query and a stored row are unified, so the row's
variables bind as well, and every use of a stored row renames it apart: two
conjuncts, or a read nested in another, that meet one row meet two
renamings of it.  Seeded scenarios (two_way_scenarios.py) put rows with
repeated variables, nested terms and variable heads into atom, set, strict
and &self spaces, past the sizes at which a space reads its indexes (the
match trie, the path index, the substitution tree, a dense relation), and
interleave single reads, conjunctions that share a variable, reads inside
function bodies, equation calls, memoized calls, transform reads and takes,
additions and removals; the collision scenarios add a row and read it in one
statement, with a pattern that may name the row's own variables.  Every
answer bag is compared with the reference model in each dialect: HE, and
PeTTa on both evaluator routes (CETTA_PETTA_SEARCH_MACHINE=0 and 1).  Every
match is also counted and observed for emptiness by the routes that answer
only those, and each must agree with the same run's enumeration.  Each
difference is shown with its scenario's plain formulation, a space that reads
its rows in turn, run in the same configuration: whether that agrees with the
model tells an index's fault from a dialect law the model lacks.
Comparisons are counts and bags of answers modulo renaming, never timings.
The scenarios depend only on their seeds.

A difference is expected only where PENDING names its flavour and
configuration, until the step that fixes it lands; a pending pair that
agrees fails the run, so that its step removes the entry.

usage: check_two_way_matching.py CETTA [--seeds N] [--jobs N] [--show N]
"""

import argparse
import collections
import concurrent.futures
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import two_way_scenarios as scenarios  # noqa: E402
from two_way_terms import bag, parse_line, show  # noqa: E402

# HE's default printer can show two different variables of one answer
# alike; --pretty-vars keeps distinct variables apart.
CONFIGS = {
    'he': ('he', ['--profile', 'extended', '--pretty-vars'], {}),
    'petta-machine-off': ('petta', ['--profile', 'extended'],
                          {'CETTA_PETTA_SEARCH_MACHINE': '0'}),
    'petta-machine-on': ('petta', ['--profile', 'extended'],
                         {'CETTA_PETTA_SEARCH_MACHINE': '1'}),
}
TIMEOUT_SECONDS = 300

# Known differences, by (flavour, configuration, observation), and the step
# that fixes them.  An observation is 'answer' (an enumeration against the
# model), or 'count' or 'emptiness' (against the same run's enumeration).
PENDING = {}

# Reads that are also counted and observed for emptiness.
CHECKED_KINDS = ('match', 'list', 'addmatch')


def generate(seeds):
    """(name, ops, needs_transform) for every seed and flavour."""
    for seed in range(seeds):
        for kind in ('atom', 'set', 'strict', 'self'):
            for nrows in (10, 40, 150):
                s = seed * 1000 + nrows + {'atom': 1, 'set': 2, 'strict': 3, 'self': 4}[kind]
                yield ('space-%s-%d-s%d' % (kind, nrows, seed),
                       scenarios.flavour_space(s, kind, nrows, 26 if nrows < 100 else 45),
                       False)
        for kind in ('atom', 'set'):
            for nrows in (12, 60):
                s = seed * 1000 + nrows + 7 + (kind == 'set')
                yield ('transform-%s-%d-s%d' % (kind, nrows, seed),
                       scenarios.flavour_space(s, kind, nrows, 24, with_transform=True),
                       True)
        for kind in ('atom', 'self'):
            for nrows in (12, 80):
                s = seed * 1000 + nrows + 11 + (kind == 'self')
                yield ('function-%s-%d-s%d' % (kind, nrows, seed),
                       scenarios.flavour_function(s, kind, nrows, 8), False)
        for neqs in (4, 20, 64):
            for late in (0, 3):
                s = seed * 1000 + neqs + 13 + late
                yield ('equations-%d-late%d-s%d' % (neqs, late, seed),
                       scenarios.flavour_equations(s, neqs, 18, late), False)
        for neqs in (6, 30):
            for arity in (1, 2):
                s = seed * 1000 + neqs + 19 + arity
                yield ('memo-%d-a%d-s%d' % (neqs, arity, seed),
                       scenarios.flavour_memo(s, neqs, 10, arity), False)
        for nrows in (41, 90):
            s = seed * 1000 + nrows + 23
            yield ('collide-%d-s%d' % (nrows, seed),
                   scenarios.flavour_collide(s, nrows, 20), False)
        s = seed * 1000 + 29
        yield ('collideq-64-s%d' % seed,
               scenarios.flavour_collide_equations(s, 64, 12), False)
        for nrows in (12, 60, 160):
            s = seed * 1000 + nrows + 31
            yield ('promote-%d-s%d' % (nrows, seed),
                   scenarios.flavour_promote(s, nrows, 24), False)
        for nrows in (10, 60):
            s = seed * 1000 + nrows + 37
            yield ('module-%d-s%d' % (nrows, seed),
                   scenarios.flavour_module(s, nrows, 8, 16), True)
        if seed < 3:
            yield ('dense-s%d' % seed,
                   scenarios.flavour_dense(seed * 1000 + 17, 4400, 12, 40), False)


def run(cetta, config, text, workdir, files=None):
    """Run TEXT, with FILES (name -> text) beside it for it to import."""
    lang, flags, env = CONFIGS[config]
    for name, body in (files or {}).items():
        # Written whole, then renamed into place: a run of another
        # configuration may be importing the same file.
        with tempfile.NamedTemporaryFile('w', suffix='.part', dir=workdir,
                                         delete=False) as f:
            f.write(body)
        os.replace(f.name, os.path.join(workdir, name))
    with tempfile.NamedTemporaryFile('w', suffix='.metta', dir=workdir,
                                     delete=False) as f:
        f.write(text)
        path = f.name
    try:
        full_env = dict(os.environ)
        full_env.update(env)
        try:
            p = subprocess.run([cetta, '--lang', lang] + flags + [path],
                               cwd=workdir, env=full_env, capture_output=True,
                               text=True, timeout=TIMEOUT_SECONDS)
            return p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            return 124, '', 'timed out after %d s' % TIMEOUT_SECONDS
    finally:
        os.unlink(path)


def run_many(cetta, jobs, workers, workdir):
    """JOBS: (key, config, text, files).  The results by key."""
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        futures = {ex.submit(run, cetta, cfg, text, workdir, files): key
                   for key, cfg, text, files in jobs}
        for future in concurrent.futures.as_completed(futures):
            results[futures[future]] = future.result()
    return results


def parse_answers(stdout):
    """(answers, counts, emptiness): tag -> the answer term X of a printed
    (r tag X), the count N of (rc tag N) and the emptiness E of (re tag E);
    (r tag (rcx X N E)) gives all three."""
    answers, counts, empties = {}, {}, {}
    for line in stdout.splitlines():
        line = line.strip()
        if not line.startswith(('(r', '[(r')):
            continue
        try:
            terms = parse_line(line)
        except (ValueError, IndexError):
            continue
        for t in terms:
            if not (t[0] == 'e' and len(t[1]) == 3 and t[1][1][0] == 'i'):
                continue
            head, tag, value = t[1][0], t[1][1][1], t[1][2]
            if head == ('s', 'r'):
                if value[0] == 'e' and len(value[1]) == 4 \
                        and value[1][0] == ('s', 'rcx'):
                    answers.setdefault(tag, value[1][1])
                    counts.setdefault(tag, value[1][2])
                    empties.setdefault(tag, value[1][3])
                else:
                    answers.setdefault(tag, value)
            elif head == ('s', 'rc'):
                counts.setdefault(tag, value)
            elif head == ('s', 're'):
                empties.setdefault(tag, value)
    return answers, counts, empties


def observation(kind):
    """The observation a difference of KIND belongs to."""
    return kind if kind in ('count', 'emptiness') else 'answer'


def checked_tags(ops):
    """The tags of the reads that are also counted and observed."""
    return [op[1] for op in ops if op[0] in CHECKED_KINDS]


def classify(expected_terms, got_term):
    """'ok' or the kind of the difference."""
    if got_term is None:
        return 'no-output'
    if got_term[0] != 'e':
        return 'not-a-list'
    want = collections.Counter(bag(expected_terms))
    have = collections.Counter(bag(list(got_term[1])))
    if want == have:
        return 'ok'
    if any(show(t).startswith('(Error') for t in got_term[1]):
        return 'error'
    lost = want - have
    extra = have - want
    if set(want) == set(have):
        return 'multiplicity'
    if lost and not extra:
        return 'lost'
    if extra and not lost:
        return 'extra'
    return 'wrong-binding'


def compare(expected, parsed, checked=()):
    """(tag, kind, expected, got) for every tagged answer that differs from
    the model, and for every count or emptiness of a CHECKED read that
    differs from the same run's enumeration of that read."""
    answers, counts, empties = parsed
    out = []
    for tag in sorted(checked):
        got = answers.get(tag)
        if got is None or got[0] != 'e':
            continue  # the enumeration's own difference is reported below
        n = len(got[1])
        if counts.get(tag) != ('i', n):
            out.append((tag, 'count', [('i', n)], counts.get(tag)))
        empty = ('e', (('s', 'none'),)) if n == 0 else ('e', ())
        if empties.get(tag) != empty:
            out.append((tag, 'emptiness', [empty], empties.get(tag)))
    for tag in sorted(expected):
        kind, terms = expected[tag]
        got = answers.get(tag)
        if kind == 'remove':
            if got is None:
                out.append((tag, 'no-output', terms, got))
            elif bag([got]) != bag(terms):
                out.append((tag, 'remove-answer', terms, got))
            continue
        c = classify(terms, got)
        if c != 'ok':
            out.append((tag, c, terms, got))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('cetta')
    ap.add_argument('--seeds', type=int, default=20)
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--show', type=int, default=8,
                    help='differences to print in full')
    args = ap.parse_args()
    cetta = os.path.abspath(args.cetta)

    jobs, meta = [], {}
    only = os.environ.get('TWOWAY_ONLY')
    for name, ops, needs_transform in generate(args.seeds):
        if only and not any(name.startswith(p) for p in only.split(',')):
            continue
        for config in CONFIGS:
            dialect = CONFIGS[config][0]
            if name.startswith('memo') and dialect == 'he':
                continue  # lib_memo is a PeTTa library
            text = scenarios.program(ops, transform_lib=needs_transform,
                                     checks=True)
            meta[(name, config)] = (ops, dialect, needs_transform)
            jobs.append(((name, config), config, text,
                         scenarios.module_files(ops)))

    with tempfile.TemporaryDirectory(prefix='cetta-two-way-') as workdir:
        results = run_many(cetta, jobs, args.jobs, workdir)
        checked = collections.Counter()
        observed = collections.Counter()
        exits = collections.Counter()
        differing = []
        for key in sorted(results):
            name, config = key
            ops, dialect, _ = meta[key]
            code, stdout, _ = results[key]
            flavour = name.split('-')[0]
            exits[(config, code)] += 1
            expected = scenarios.expected(ops, dialect)
            checked[(flavour, config)] += len(expected)
            observed[(flavour, config)] += 2 * len(checked_tags(ops))
            diffs = compare(expected, parse_answers(stdout), checked_tags(ops))
            if diffs:
                differing.append((key, diffs))

        # The plain formulation of every scenario that differs, in the same
        # configuration: rows read in turn.
        plain_jobs, plain_ops = [], {}
        for key, _ in differing:
            name, config = key
            ops, dialect, needs_transform = meta[key]
            plain = (scenarios.equation_plain_formulation(ops)
                     if name.startswith(('equations', 'collideq'))
                     else scenarios.plain_formulation(ops))
            plain_ops[key] = plain
            plain_jobs.append((key, config,
                               scenarios.program(plain, transform_lib=needs_transform,
                                                 checks=True),
                               scenarios.module_files(plain)))
        plain_results = run_many(cetta, plain_jobs, args.jobs, workdir)

    differences = collections.Counter()
    shown = 0
    for key, diffs in differing:
        name, config = key
        ops, dialect, _ = meta[key]
        code, _, stderr = results[key]
        _, plain_stdout, _ = plain_results[key]
        plain_diffs = {(d[0], observation(d[1])) for d in compare(
            scenarios.expected(plain_ops[key], dialect),
            parse_answers(plain_stdout), checked_tags(plain_ops[key]))}
        flavour = name.split('-')[0]
        for tag, kind, want, have in diffs:
            agrees = (tag, observation(kind)) not in plain_diffs
            differences[(flavour, config, kind,
                         'plain formulation agrees with the model' if agrees
                         else 'plain formulation differs too')] += 1
            if shown < args.show and \
                    (flavour, config, observation(kind)) not in PENDING:
                shown += 1
                print('DIFF %s %s tag %d: %s (exit %d); plain formulation %s'
                      % (name, config, tag, kind, code,
                         'agrees' if agrees else 'differs too'))
                print('  expected: %s' % ' '.join(show(t) for t in want))
                print('  got:      %s' % (show(have) if have is not None else None))
                tail = stderr.strip().splitlines()[-1:] if stderr.strip() else []
                if tail:
                    print('  stderr:   %s' % tail[0][:200])

    print('answers checked by flavour and configuration:')
    for flavour, config in sorted(checked):
        print('  %s %s: %d, and %d counts and emptiness checks'
              % (flavour, config, checked[(flavour, config)],
                 observed[(flavour, config)]))
    print('runs by configuration and exit code:')
    for config, code in sorted(exits):
        print('  %s exit %d: %d' % (config, code, exits[(config, code)]))
    bad_exits = sum(n for (config, code), n in exits.items() if code != 0)
    def entry(k):
        return (k[0], k[1], observation(k[2]))
    expected_diffs = {k: n for k, n in differences.items() if entry(k) in PENDING}
    unexpected = {k: n for k, n in differences.items() if entry(k) not in PENDING}
    if expected_diffs:
        print('expected differences, until the step that fixes them:')
        for k in sorted(expected_diffs):
            print('  %s %s %s (%s): %d, until %s'
                  % (k[0], k[1], k[2], k[3], expected_diffs[k], PENDING[entry(k)]))
    stale = sorted(e for e in PENDING
                   if checked[e[:2]] and not any(entry(k) == e for k in differences))
    for flavour, config, what in stale:
        print('PENDING ENTRY NOW AGREES: %s %s %s; its step (%s) removes it'
              % (flavour, config, what, PENDING[(flavour, config, what)]))
    if unexpected or bad_exits or stale:
        if unexpected:
            print('differences by flavour, configuration and kind:')
            for k in sorted(unexpected):
                print('  %s %s %s (%s): %d' % (k[0], k[1], k[2], k[3], unexpected[k]))
        print('FAIL: two-way matching: %d answers or checks differ outside '
              'the pending entries, %d runs exited non-zero, %d pending '
              'entries now agree'
              % (sum(unexpected.values()), bad_exits, len(stale)))
        return 1
    print('PASS: two-way matching: %d answers in HE and both PeTTa routes and '
          '%d counts and emptiness checks, over %d seeds, agree with the model '
          'and with their enumerations outside the pending entries%s'
          % (sum(checked.values()), sum(observed.values()), args.seeds,
             '; pending until their steps: %d differences (%s)'
             % (sum(expected_diffs.values()),
                ', '.join('%s %s %s' % e for e in sorted(PENDING) if checked[e[:2]]))
             if expected_diffs else ''))
    return 0

if __name__ == '__main__':
    sys.exit(main())
