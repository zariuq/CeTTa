#!/usr/bin/env python3
"""Compare the GSLT reading of .mg files with Megalodon's own lexer and parser.

The GSLT presentations under langdef/megalodon/ are compiled to one PeTTa
program (langdef-compile petta-direct).  A PeTTa engine runs it on each file,
one token (MgNextTokenV1) or one item (MgReadItemV1) per step, printing each
as it is read; this script puts them in the oracles' formats, Megalodon's
tokens (--mode tokens) or its document items and proof tactics (--mode
items), and compares them with the oracles' output or with the expectations
recorded beside each fixture (--expected).  Lists of Unicode scalars become
OCaml-quoted byte strings, as the oracles print them.

Engines: CeTTa's PeTTa lane (--engine cetta, the file read by the Megalodon
source langdef) and the presentations compiled to C (--engine native, the
reader mg-gslt-native).

Where Megalodon refuses a file, the readings before its error must be the
same, and the error too: the native engine prints Megalodon's message with
its positions, which must be the oracle's to the character; the PeTTa
engine prints the error the presentations answer, whose kind (parse error or
failure) and message must be the oracle's.  Its positions are the reader's
bookkeeping over the presentations' streams, done by the compiled reader.
"""

import re

import argparse
import pathlib
import subprocess
import sys
import time


def parse_sexprs(text):
    tokens = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in ' \t\r\n':
            i += 1
        elif c in '()':
            tokens.append(c)
            i += 1
        else:
            j = i
            while j < n and text[j] not in ' \t\r\n()':
                j += 1
            tokens.append(text[i:j])
            i = j
    out = []
    stack = [out]
    for tok in tokens:
        if tok == '(':
            stack.append([])
        elif tok == ')':
            done = stack.pop()
            stack[-1].append(done)
        else:
            stack[-1].append(tok)
    return out


def ocaml_quote(data):
    out = ['"']
    for b in data:
        if b == 34:
            out.append('\\"')
        elif b == 92:
            out.append('\\\\')
        elif b == 10:
            out.append('\\n')
        elif b == 9:
            out.append('\\t')
        elif b == 13:
            out.append('\\r')
        elif b == 8:
            out.append('\\b')
        elif 32 <= b <= 126:
            out.append(chr(b))
        else:
            out.append('\\%03d' % b)
    out.append('"')
    return ''.join(out)


def scalars(term):
    """An MgCp list of (cp N) as bytes (UTF-8)."""
    chars = []
    while isinstance(term, list) and term and term[0] == 'MgCpConsV1':
        chars.append(chr(int(term[1][1])))
        term = term[2]
    if term != 'MgCpNilV1':
        raise ValueError('not a scalar list: %r' % (term,))
    # a byte that is not UTF-8 was read as U+DC80..U+DCFF
    return ''.join(chars).encode('utf-8', 'surrogateescape')


def quoted(term):
    return ocaml_quote(scalars(term))


def option(term):
    if term == 'MgNoneV1':
        return 'None'
    return '(Some %s)' % quoted(term[1])


def token_text(term):
    if isinstance(term, str):
        assert term.startswith('MgTk') and term.endswith('V1'), term
        return term[4:-2].upper()
    head = term[0]
    named = {'MgTkNamV1': 'NAM', 'MgTkStringV1': 'STRING', 'MgTkQStringV1': 'QUESTIONSTRING',
             'MgTkTheoremV1': 'THEOREM', 'MgTkSpecCommV1': 'SPECCOMM'}
    if head in named:
        return '(%s %s)' % (named[head], quoted(term[1]))
    if head == 'MgTkNumV1':
        neg = 'True' if term[1] == 'MgTrueV1' else 'False'
        return '(NUM %s %s %s %s)' % (neg, quoted(term[2]), option(term[3]), option(term[4]))
    raise ValueError('unknown token %r' % (term,))


def item_shape(term):
    """A reading tree node as (children, combine), or (None, text) for a leaf."""
    if isinstance(term, str):
        if term == 'MgCpNilV1':
            return None, '""'
        if term == 'MgListNilV1':
            return None, '(List)'
        assert term.startswith('Mg') and term.endswith('V1'), term
        return None, term[2:-2]
    head = term[0]
    if head == 'MgCpConsV1':
        return None, quoted(term)
    if head == 'MgIntV1':
        return None, str(int(scalars(term[1]).decode()))
    if head == 'MgListConsV1':
        items = []
        while isinstance(term, list) and term[0] == 'MgListConsV1':
            items.append(term[1])
            term = term[2]
        assert term == 'MgListNilV1', term
        return items, lambda xs: '(List%s)' % ''.join(' ' + x for x in xs)
    if head == 'MgPairV1':
        return term[1:3], lambda xs: '(%s %s)' % (xs[0], xs[1])
    if head == 'MgDocV1':
        return term[1:2], lambda xs: '(Doc %s)' % xs[0]
    if head == 'MgPfTacV1':
        return term[1:2], lambda xs: '(PfTac %s)' % xs[0]
    assert head.startswith('Mg') and head.endswith('V1'), head
    name = head[2:-2]
    return term[1:], lambda xs: '(%s %s)' % (name, ' '.join(xs))


def item_text(term):
    """A GSLT reading tree in the parse oracle's format.  Reading trees
    nest as deeply as the terms they read, so the walk keeps its own
    stack."""
    values = []
    work = [(False, term)]
    while work:
        combine, x = work.pop()
        if combine:
            fn, n = x
            args = values[len(values) - n:]
            del values[len(values) - n:]
            values.append(fn(args))
            continue
        children, fn = item_shape(x)
        if children is None:
            values.append(fn)
            continue
        work.append((True, (fn, len(children))))
        for child in reversed(children):
            work.append((False, child))
    return values[0]


# Plain PeTTa around the compiled presentations: one token or one item per
# step, printed as it is read.
READER_FUNCTIONS = '''
(= (mg-next-token $s) (let (gslt:result:MgNextTokenV1:10 $r) (once (gslt:fn:MgNextTokenV1:10 $s)) $r))
(= (mg-tokens MgTokensEndV1) mg-done)
(= (mg-tokens (MgAfterNewlineV1 $at $read)) (mg-tokens $read))
(= (mg-tokens (MgAfterCommentV1 $open $after $read)) (mg-tokens $read))
(= (mg-tokens (MgTokenReadV1 $t (MgScalarsV1 $rest))) (let $_ (println! $t) (mg-tokens (mg-next-token $rest))))
(= (mg-tokens (MgLexFailV1 $after)) (let $_ (println! mg-lexing-failure) mg-error))
(= (mg-read-item $reader) (let (gslt:result:MgReadItemV1:10 $o) (once (gslt:fn:MgReadItemV1:10 $reader)) $o))
(= (mg-start $s) (let (gslt:result:MgReaderStartV1:10 $r) (gslt:fn:MgReaderStartV1:10 $s) $r))
(= (mg-next-file $end $s) (let (gslt:result:MgReaderNextFileV1:110 $r) (gslt:fn:MgReaderNextFileV1:110 $end $s) $r))
(= (mg-items (MgReaderEndV1 $m $e)) mg-done)
(= (mg-items (MgItemV1 $item $next)) (let $_ (println! $item) (mg-items (mg-read-item $next))))
(= (mg-items (MgReaderErrorV1 (MgSyntaxErrorV1 $m $start $end))) (let $_ (println! (mg-syntax-error $m)) mg-error))
(= (mg-items (MgReaderErrorV1 (MgFailureV1 $m $at))) (let $_ (println! (mg-failure $m)) mg-error))
(= (mg-skip (MgReaderEndV1 $m $e)) (MgReaderEndV1 $m $e))
(= (mg-skip (MgItemV1 $item $next)) (mg-skip (mg-read-item $next)))
(= (mg-skip (MgReaderErrorV1 $e)) (MgReaderErrorV1 $e))
(= (mg-then (MgReaderEndV1 $m $e) $s) (mg-read-item (mg-next-file (MgReaderEndV1 $m $e) $s)))
(= (mg-then (MgReaderErrorV1 $e) $s) (MgReaderErrorV1 $e))
'''


def reading(mode, scalars_of, path, signatures):
    """The PeTTa expression that prints PATH's tokens or items, after its
    signatures; SCALARS_OF makes the expression for a file's scalars."""
    if mode == 'tokens':
        return '(mg-tokens (mg-next-token %s))' % scalars_of(path)
    if not signatures:
        return '(mg-items (mg-read-item (mg-start %s)))' % scalars_of(path)
    end = '(mg-skip (mg-read-item (mg-start %s)))' % scalars_of(signatures[0])
    for sig in signatures[1:]:
        end = '(mg-skip (mg-then %s %s))' % (end, scalars_of(sig))
    return '(mg-items (mg-then %s %s))' % (end, scalars_of(path))


def converted(mode, lines):
    convert = token_text if mode == 'tokens' else item_text
    return [convert(parse_sexprs(line)[0]) for line in lines]


def message_text(parts):
    """An error's message parts, texts and (MgIntV1 digits), as one text."""
    out = []
    while isinstance(parts, list) and parts and parts[0] == 'MgListConsV1':
        part = parts[1]
        if isinstance(part, list) and part[0] == 'MgIntV1':
            out.append(str(int(scalars(part[1]).decode())))
        else:
            out.append(scalars(part).decode('utf-8', 'surrogateescape'))
        parts = parts[2]
    if parts != 'MgListNilV1':
        raise ValueError('not a message: %r' % (parts,))
    return ''.join(out)


def error_kind(line):
    """The kind and message of the error the PeTTa reading printed."""
    if line == 'mg-lexing-failure':
        return ('failure', 'lexing: empty token')
    term = parse_sexprs(line)[0]
    kinds = {'mg-syntax-error': 'parse error', 'mg-failure': 'failure'}
    if not isinstance(term, list) or term[0] not in kinds:
        raise ValueError('not an error: %r' % (line,))
    return (kinds[term[0]], message_text(term[1]))


def normalized_error(text):
    """Megalodon's refusal as the parse oracle prints it: the token oracle's
    uncaught lexing failure and the parse oracle's failures read alike."""
    text = text.strip()
    m = re.fullmatch(r'Fatal error: exception Failure\("(.*)"\)', text)
    if m:
        return 'failure: ' + m.group(1)
    return text.replace('oracle failure: ', 'failure: ', 1) if text.startswith('oracle failure: ') else text


def message_kind(message):
    """A normalized refusal's kind and message, its positions left out."""
    m = re.fullmatch(r'parse error \d+:\d+-\d+:\d+: (.*)', message, re.S)
    if m:
        return ('parse error', m.group(1))
    if message.startswith('failure: '):
        return ('failure', message[len('failure: '):])
    return ('other', message)


CETTA_DRIVER = '''
!(import! &self langdef)
(= (mg-scalars $mg $path)
   (let (LangDef:ParseAccepted $name $digest ((node mg-source $cps)))
        (langdef:parse-file $mg $path) $cps))
!(let $mg (langdef:load "langdef/megalodon/langdef.metta")
   (let $answers (collapse %s)
        (if (== $answers ()) (println! mg-run-refused)
            (if (== $answers (mg-error)) (println! mg-run-error) (println! mg-run-end)))))
'''


# Outcomes of one reading.  OK agrees with Megalodon's reading, and ERROR
# (the readings before it, and its message) with its refusal.  The
# presentations having no reading (REFUSED), an engine that crashes
# (ENGINE), runs out of time (TIMEOUT) or prints something else (MALFORMED)
# never agree.
OK, ERROR, REFUSED, ENGINE, TIMEOUT, MALFORMED = 'OK', 'ERROR', 'REFUSED', 'ENGINE', 'TIMEOUT', 'MALFORMED'


def engine_failure(done):
    tail = done.stderr.decode('utf-8', 'replace')[-400:]
    return (ENGINE, 'exit %d: %s' % (done.returncode, tail))


MARKS = {'mg-run-end': OK, 'mg-run-error': ERROR, 'mg-run-refused': REFUSED}


def read_out(mode, done, native):
    """The outcome a reading printed: its readings in the oracle's format,
    and for ERROR its message (the native engine's on stderr, the PeTTa
    engine's as the error it printed last), or MALFORMED."""
    lines = done.stdout.decode('utf-8', 'replace').splitlines()
    for marker, outcome in MARKS.items():
        if marker not in lines:
            continue
        before = lines[:lines.index(marker)]
        errors = [l for l in before if l.startswith('(mg-') or l == 'mg-lexing-failure']
        printed = [l for l in before if (l.startswith('(') or l.startswith('Mg')) and not l.startswith('(mg-')]
        try:
            readings = converted(mode, printed)
            if outcome == OK:
                return (OK, readings)
            if outcome == REFUSED:
                return (REFUSED, readings)
            if native:
                return (ERROR, (readings, done.stderr.decode('utf-8', 'replace').strip()))
            if len(errors) != 1:
                return (MALFORMED, 'errors printed: %r' % (errors,))
            return (ERROR, (readings, error_kind(errors[0])))
        except (AssertionError, ValueError, IndexError) as exc:
            return (MALFORMED, 'unreadable output: %r' % (exc,))
    tail = (done.stdout + done.stderr).decode('utf-8', 'replace')[-400:]
    return (MALFORMED, 'no end mark: %s' % tail)


def run_cetta(cetta, program, path, mode, work, timeout, signatures):
    driver = work / 'driver.metta'
    expression = reading(mode, lambda p: '(mg-scalars $mg "%s")' % p, path, signatures)
    driver.write_text(program + READER_FUNCTIONS + CETTA_DRIVER % expression)
    started = time.perf_counter()
    try:
        done = subprocess.run(
            [cetta, '--lang', 'petta', str(driver)], capture_output=True,
            timeout=timeout, env={'CETTA_PETTA_SEARCH_MACHINE': '1', 'PATH': '/usr/bin:/bin'})
    except subprocess.TimeoutExpired:
        return (TIMEOUT, 'after %ss' % timeout), timeout
    elapsed = time.perf_counter() - started
    if done.returncode != 0:
        return engine_failure(done), elapsed
    return read_out(mode, done, native=False), elapsed


def run_native(native, path, mode, timeout, signatures):
    """The presentations compiled to C (langdef-compile petta-direct --target c,
    tests/megalodon/mg_gslt_native.c), printing what the PeTTa reading prints."""
    cmd = [native, '--tokens' if mode == 'tokens' else '--items']
    for sig in signatures:
        cmd += ['-I', sig]
    cmd.append(path)
    started = time.perf_counter()
    try:
        done = subprocess.run(cmd, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return (TIMEOUT, 'after %ss' % timeout), timeout
    elapsed = time.perf_counter() - started
    if done.returncode != 0:
        return engine_failure(done), elapsed
    return read_out(mode, done, native=True), elapsed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', choices=['cetta', 'native'], default='cetta')
    parser.add_argument('--native', help='the compiled reader (mg-gslt-native) for --engine native')
    parser.add_argument('--cetta', help='CeTTa binary for --engine cetta')
    parser.add_argument('--program', help='compiled PeTTa program (--engine cetta)')
    parser.add_argument('--mode', choices=['tokens', 'items'], default='tokens')
    parser.add_argument('--token-oracle')
    parser.add_argument('--parse-oracle')
    parser.add_argument('--expected', action='store_true',
                        help='compare with FILE.tokens or FILE.items beside each FILE.mg, and '
                             'FILE.err, Megalodon\'s refusal (empty when it reads the file to the end)')
    parser.add_argument('--includes', help='TSV of FILE<TAB>SIGNATURE relative to --root')
    parser.add_argument('--root')
    parser.add_argument('--work', required=True)
    parser.add_argument('--timeout', type=float, default=600.0)
    parser.add_argument('files', nargs='+')
    args = parser.parse_args()
    program = pathlib.Path(args.program).read_text() if args.program else ''
    if args.engine != 'native' and not program:
        parser.error('--program is required for the %s engine' % args.engine)
    work = pathlib.Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    includes = {}
    if args.includes:
        base = pathlib.Path(args.root)
        for line in pathlib.Path(args.includes).read_text().splitlines():
            if line and not line.startswith('#'):
                name, sig = line.split('\t')
                includes[(base / name).resolve()] = [str((base / sig).resolve())]
    oracle = args.token_oracle if args.mode == 'tokens' else args.parse_oracle
    tally = {'agree': 0, 'differ': 0}
    times = {'oracle': 0.0, 'gslt': 0.0}

    def expectation(path):
        """The oracle's readings of PATH, and its refusal (None when
        Megalodon reads the file to the end)."""
        sigs = includes.get(path, []) if args.mode == 'items' else []
        started = time.perf_counter()
        if args.expected:
            stem = str(path.with_suffix(''))
            expected = pathlib.Path(stem + ('.tokens' if args.mode == 'tokens' else '.items')).read_text().splitlines()
            err = pathlib.Path(stem + '.err')
            refusal = normalized_error(err.read_text()) if err.exists() else ''
            # Megalodon's parser lexes as it reads: FILE.err's lexing
            # failure is also the lexer's refusal; any other is the parser's.
            if args.mode == 'tokens' and 'lexing' not in refusal:
                refusal = ''
        else:
            extra = []
            for sig in sigs:
                extra += ['-I', sig]
            ref = subprocess.run([oracle, *extra, str(path)], capture_output=True)
            expected = ref.stdout.decode('utf-8', 'replace').splitlines()
            refusal = normalized_error(ref.stderr.decode('utf-8', 'replace')) if ref.returncode != 0 else ''
        times['oracle'] += time.perf_counter() - started
        return sigs, expected, refusal or None

    native = args.engine == 'native'

    def judge(name, expected, refusal, got):
        status, payload = got
        if refusal is None:
            agree = status == OK and payload == expected
        else:
            message = refusal if native else message_kind(refusal)
            agree = status == ERROR and payload == (expected, message)
        if agree:
            tally['agree'] += 1
            return
        tally['differ'] += 1
        print('DIFFER', name, 'oracle', 'reads' if refusal is None else 'refuses: %s' % refusal[:200],
              'gslt', status, flush=True)
        readings = payload[0] if status == ERROR else payload if status in (OK, REFUSED) else None
        if status == ERROR and payload[0] == expected:
            print('  error', str(payload[1])[:300])
        elif readings is not None:
            for k, (a, b) in enumerate(zip(expected, readings)):
                if a != b:
                    print('  line', k, 'oracle', a[:200], '\n    gslt  ', b[:200])
                    break
            else:
                print('  lengths', len(expected), len(readings))
        else:
            print('  ', str(payload)[:300])

    paths = [pathlib.Path(name).resolve() for name in args.files]
    for path in paths:
        sigs, expected, refusal = expectation(path)
        if args.engine == 'native':
            got, elapsed = run_native(args.native, str(path), args.mode, args.timeout, sigs)
        else:
            got, elapsed = run_cetta(args.cetta, program, str(path), args.mode, work, args.timeout, sigs)
        times['gslt'] += elapsed
        judge(str(path), expected, refusal, got)
    print('files %d agree %d differ %d; time oracle %.2fs gslt %.2fs' % (
        len(paths), tally['agree'], tally['differ'], times['oracle'], times['gslt']))
    return 0 if tally['differ'] == 0 else 1


def main_deep():
    """Reading trees nest as deeply as the terms they read: run with a
    large stack and recursion limit."""
    import threading
    result = []
    sys.setrecursionlimit(1 << 20)
    threading.stack_size(1 << 29)
    worker = threading.Thread(target=lambda: result.append(main()))
    worker.start()
    worker.join()
    return result[0] if result else 1


if __name__ == '__main__':
    sys.exit(main_deep())
