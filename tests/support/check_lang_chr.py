#!/usr/bin/env python3
"""Qualify the public CeTTa lang library through real ground CHR (C0) execution.

Each case runs ordinary CHR source through (lang:load CHR): the in-process
source compiler reads and prepares it, and the MeTTa CHR controller runs it.
lang:eval is private; lang:run transforms the captured constraint occurrences.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def quote(text):
    return json.dumps(text, ensure_ascii=False)


def raw(text):
    depth = 1
    while '"' + '#' * depth in text:
        depth += 1
    return 'r' + '#' * depth + '"' + text + '"' + '#' * depth


COMMON = '''!(import! &self lang)
(= (check:status (LangCompleteV1 $store)) Complete)
(= (check:status (LangPendingV1 $cursor)) Pending)
(= (check:status LangGuestFailureV1) GuestFailure)
(= (check:status (LangGuestFaultV1 $reason)) GuestFault)
(= (check:status (LangPublicationRefusedV1 $reason)) PublicationRefused)
(= (check:status (LangSourceErrorV1 $message)) Rejected)
(= (check:status (LangSourceReadV1 $source $result)) ReadRejected)
(= (check:status (LangDefinitionUnadmittedV1 $module $reason)) Unadmitted)
(= (check:status (LangRunUnadmittedV1 $module $source)) RunUnadmitted)
(= (check:status (LangArgumentErrorV1 $expected $actual)) ArgumentRejected)
(= (check:rows $space $pattern) (collapse (match $space $pattern $pattern)))
(= (check:count $space $pattern) (size-atom (collapse (match $space $pattern $pattern))))
; Resume one unit at a time; record the matching row count whenever it changes.
(= (check:note $seen $trace)
   (if (== $trace ()) ($seen) (if (== (car-atom $trace) $seen) $trace (cons-atom $seen $trace))))
(= (check:drive $pending $left $space $pattern $trace)
   (if (== $left 0) (Exhausted $trace)
     (let $next (lang:resume $pending 1)
       (let $noted (check:note (check:count $space $pattern) $trace)
         (case $next
           (((LangCompleteV1 $store) (Finished $noted))
            ((LangPendingV1 $cursor) (check:drive $pending (- $left 1) $space $pattern $noted))
            ($other (Failed $other))))))))
'''

MINIMUM = ':- chr_constraint minimum/1.\nminimum_remove @ minimum(X) \\ minimum(Y) <=> X =< Y | true.\n'
GCD = ':- chr_constraint gcd/1.\ngcd_zero @ gcd(0) <=> true.\ngcd_reduce @ gcd(N) \\ gcd(M) <=> N =< M | L is M mod N, gcd(L).\n'
PROPAGATION = ':- chr_constraint signal/1,published/1.\npublish @ signal(X) ==> published(X).\n'


def cases(engine):
    module_path = engine / 'langdef/lang/CHR.module'
    chr_ = '(lang:load CHR)'
    out = {}

    def run(text, destination='&self', budget='1000000', language=chr_):
        return f'(lang:run {language} {destination} {destination} {budget} {raw(text)})'

    query_min = MINIMUM + '?- minimum(4),minimum(2),minimum(2),minimum(9).\n'
    out['eval-query-private'] = f'''!(test (lang:eval {chr_} {raw(query_min)}) (LangCompleteV1 ((minimum 2))))
!(test (check:rows &self (minimum $x)) ())
'''
    out['layout-vertical-and-formfeed'] = f'''!(test (lang:eval {chr_} {raw(query_min.replace(' ', chr(11)) + chr(12))}) (LangCompleteV1 ((minimum 2))))
'''
    out['eval-from-space'] = '''(minimum 4)
(minimum 2)
(minimum 2)
(minimum 9)
(unrelated keep)
''' + f'''!(test (lang:eval {chr_} {raw(MINIMUM)}) (LangCompleteV1 ((minimum 2))))
!(test (check:count &self (minimum $x)) 4)
'''
    # Exact occurrences: the run consumes three captured occurrences and keeps
    # the first (minimum 2). The stable contraction keeps the relative order
    # of the remaining rows, so the survivor's identity is visible.
    out['run-exact-duplicates'] = f'''!(let* (($space (new-space))
         ($_ (add-atom $space (minimum 4))) ($_ (add-atom $space (unrelated a)))
         ($_ (add-atom $space (minimum 2))) ($_ (add-atom $space (unrelated b)))
         ($_ (add-atom $space (minimum 2))) ($_ (add-atom $space (unrelated c)))
         ($_ (add-atom $space (minimum 9))) ($_ (add-atom $space (minimum 1 2)))
         ($done (lang:run {chr_} $space {raw(MINIMUM)})))
   (test (Exact (check:status $done) (collapse (get-atoms $space))) (Exact Complete BODY)))
'''
    out['run-query-adds'] = f'''!(test (check:status {run(query_min)}) Complete)
!(test (check:rows &self (minimum $x)) ((minimum 2)))
'''
    out['run-gcd-consume-and-add'] = '''(gcd 12)
(gcd 18)
(other row)
''' + f'''!(test (check:status {run(GCD)}) Complete)
!(test (check:rows &self (gcd $x)) ((gcd 6)))
!(test (check:rows &self (other $x)) ((other row)))
'''
    out['budget-zero-and-resume'] = '''(gcd 12)
(gcd 18)
''' + f'''!(let* (($pending {run(GCD, budget='0')})
         ($first (lang:resume $pending 1))
         ($done (lang:resume $pending 1000000))
         ($again (lang:resume $pending 1000000)))
   (test (Statuses (check:status $pending) (check:status $first) (check:status $done) (check:status $again) (== $done $again))
         (Statuses Pending Pending Complete Complete True)))
!(test (check:rows &self (gcd $x)) ((gcd 6)))
'''
    # One unit per resume: the contraction is one unit, each addition another.
    out['publication-interruption'] = '''(gcd 12)
(gcd 18)
''' + f'''!(let $pending {run(GCD, budget='0')}
   (test (check:drive $pending 200000 &self (gcd $x) ()) (Finished (1 0 2))))
!(test (check:rows &self (gcd $x)) ((gcd 6)))
'''
    # Capture happens once, at invocation: later mutation changes neither the
    # inputs of a retained run nor its result.
    out['eval-capture-isolated-from-mutation'] = '''(gcd 12)
(gcd 18)
''' + f'''!(let* (($pending (lang:eval {chr_} &self 0 {raw(GCD)}))
         ($_ (remove-atom &self (gcd 12)))
         ($_ (add-atom &self (gcd 7)))
         ($done (lang:resume $pending 1000000)))
   (test $done (LangCompleteV1 ((gcd 6)))))
!(test (check:rows &self (gcd $x)) ((gcd 18) (gcd 7)))
'''
    out['distinct-output'] = f'''!(let* (($input (new-space)) ($output (new-space))
         ($_ (add-atom $input (gcd 12))) ($_ (add-atom $input (gcd 18)))
         ($_ (add-atom $output (gcd 6)))
         ($done (lang:run {chr_} $input $output {raw(GCD)})))
   (test (Separate (check:status $done) (check:rows $input (gcd $x)) (check:rows $output (gcd $y)))
         (Separate Complete ((gcd 12) (gcd 18)) ((gcd 6) (gcd 6)))))
'''
    out['stale-capture-refused'] = '''(gcd 12)
(gcd 18)
''' + f'''!(let* (($pending {run(GCD, budget='0')})
         ($_ (add-atom &self (late row)))
         ($done (lang:resume $pending 1000000)))
   (test (check:status $done) PublicationRefused))
!(test (check:rows &self (gcd $x)) ((gcd 12) (gcd 18)))
'''
    out['additions-only-after-mutation'] = '''(signal 7)
''' + f'''!(let* (($pending {run(PROPAGATION, budget='0')})
         ($_ (add-atom &self (late row)))
         ($done (lang:resume $pending 1000000)))
   (test (check:status $done) Complete))
!(test (check:rows &self (published $x)) ((published 7)))
!(test (check:rows &self (signal $x)) ((signal 7)))
'''
    out['repeated-run-refires-propagation'] = '''(signal 7)
''' + f'''!(test (check:status {run(PROPAGATION)}) Complete)
!(test (check:status {run(PROPAGATION)}) Complete)
!(test (check:count &self (published $x)) 2)
'''
    failing = ':- chr_constraint p/0.\nstop @ p <=> fail.\n?- p.\n'
    out['guest-failure'] = '''(keep me)
''' + f'''!(test (check:status {run(failing)}) GuestFailure)
!(test (check:rows &self (keep $x)) ((keep me)))
'''
    faulting = ':- chr_constraint d/1,e/1.\ndivide @ d(X) <=> Y is X // 0, e(Y).\n?- d(1).\n'
    out['guest-fault'] = f'''!(test (check:status {run(faulting)}) GuestFault)
!(test (check:rows &self (e $x)) ())
'''
    out['no-host-execution'] = '''(= (gcd 6) (add-atom &self (UnwantedEffect)))
(gcd 12)
(gcd 18)
''' + f'''!(test (check:status {run(GCD)}) Complete)
!(test (collapse (match &self (UnwantedEffect) yes)) ())
!(test (size-atom (collapse (match &self (gcd 6) yes))) 1)
'''
    for name, row in [('variable', '(gcd $x)'), ('float', '(gcd 1.5)')]:
        out['bad-input-' + name] = row + f'''\n!(test (check:status {run(GCD)}) Rejected)
!(test (check:count &self (gcd $x)) 1)
'''
    zero = ':- chr_constraint gate/0,opened/0.\nopen @ gate <=> opened.\n'
    out['zero-arity'] = '''(gate)
''' + f'''!(test (check:status {run(zero)}) Complete)
!(test (check:rows &self (opened)) ((opened)))
!(test (check:rows &self (gate)) ())
'''
    out['bare-zero-arity-inputs'] = '!(add-atom &self gate)\n!(add-atom &self "gate")\n' + f'''!(test (check:status {run(zero)}) Complete)
!(test (check:count &self (opened)) 2)
!(test (collapse (match &self gate present)) ())
!(test (collapse (match &self "gate" present)) ())
'''
    tuples = ':- chr_constraint edge/2,seen/2.\nnote @ edge(X,Y) ==> seen(X,Y).\n'
    out['tuples-and-symbols'] = '''(edge a (1 (b c) ()))
''' + f'''!(test (check:status {run(tuples)}) Complete)
!(test (check:rows &self (seen $x $y)) ((seen a (1 (b c) ()))))
'''
    signed = ':- chr_constraint n/1,out/1.\nshift @ n(X) <=> Y is X - 10, Z is Y * -3, out(Z).\n?- n(3).\n'
    out['signed-arithmetic'] = f'''!(test (lang:eval {chr_} {raw(signed)}) (LangCompleteV1 ((out 21))))
'''
    wide = (':- chr_constraint big/1,out/1.\n'
            'square @ big(X) <=> X > 18446744073709551615 | Y is X * X, Z is Y - X, out(Z).\n'
            'small @ big(X) <=> out(X).\n'
            '?- big(123456789012345678901), big(-9223372036854775809).\n')
    out['wide-integers'] = f'''!(test (lang:eval {chr_} {raw(wide)}) (LangCompleteV1 ((out 15241578753238836750313976776514250888900) (out -9223372036854775809))))
'''
    out['empty-program'] = f'''!(test (lang:eval {chr_} {raw('')}) (LangCompleteV1 ()))
'''
    unparsable = ':- chr_constraint p/1.\np(X <=> .\n'
    undeclared = ':- chr_constraint p/1.\nmove @ p(X) <=> q(X).\n?- p(1).\n'
    unsafe = ':- chr_constraint p/1,q/1.\nmove @ p(X) <=> q(Y).\n?- p(1).\n'
    out['unparsable-source'] = f'''!(test (check:status {run(unparsable)}) ReadRejected)
!(test (check:rows &self (p $x)) ())
'''
    out['undeclared-body-rejected'] = f'''!(test (check:status {run(undeclared)}) Rejected)
!(test (check:rows &self (q $x)) ())
'''
    out['unsafe-body-variable-rejected'] = f'''!(test (check:status {run(unsafe)}) Rejected)
!(test (check:rows &self (q $x)) ())
'''
    out['argument-errors'] = f'''!(test (check:status {run(GCD, budget='-1')}) ArgumentRejected)
!(test (check:status (lang:run {chr_} &self 17 {raw(GCD)})) ArgumentRejected)
'''
    for name, language in [('package-file', f'(lang:load {quote(str(module_path))})'),
                           ('package-memory', '(lang:def (lang:module (lang:load CHR)))')]:
        out[name] = '''(gcd 12)
(gcd 18)
''' + f'''!(test (check:status {run(GCD, language=language)}) Complete)
!(test (check:rows &self (gcd $x)) ((gcd 6)))
'''
    out['stack-space-copied-capture'] = f'''!(let* (($space (new-space stack))
         ($_ (add-atom $space (gcd 12))) ($_ (add-atom $space (gcd 18)))
         ($done (lang:run {chr_} $space {raw(GCD)})))
   (test (Stack (check:status $done) (check:rows $space (gcd $x))) (Stack Complete ((gcd 6)))))
'''
    # Names whose plain host-source spelling is not faithful travel as strings: each C0 value has one
    # host form, and capturing that form gives the same value back.
    for label, atom, record in [('upper-true', "'True'", '{"string":"True"}'),
                                ('upper-false', "'False'", '{"string":"False"}'),
                                ('empty-atom', "''", '{"string":""}'),
                                ('nested-upper', "tuple(a,'True')",
                                 '{"expression":[{"symbol":"a"},{"string":"True"}]}')]:
        text = f':- chr_constraint word/1.\n?- word({atom}).\n'
        both_records = ('[{"expression":[{"symbol":"word"},' + record + ']},'
                        '{"expression":[{"symbol":"seen"},' + record + ']}]')
        echo = ':- chr_constraint word/1,seen/1.\nnote @ word(X) ==> seen(X).\n'
        out['host-name-' + label] = f'''!(let* (($space (new-space))
        ($posted (lang:run {chr_} $space {raw(text)}))
        ($echoed (lang:run {chr_} $space {raw(echo)}))
        ($view (langdef:capture-occurrences $space)))
  (case $view (((SourceOccurrenceViewV1 $handle)
    (test (Lossless (check:status $posted) (check:status $echoed)
                    (langdef:occurrence-select $handle ("word" "seen")))
          (Lossless Complete Complete (SourceOccurrencesV1 {raw(both_records)})))))))
!(test (check:rows &self (word $x)) ())
'''
    # A constraint whose own name needs the string form.
    named = ":- chr_constraint 'True'/1,seen/1.\n?- 'True'(1).\n"
    named_echo = ":- chr_constraint 'True'/1,seen/1.\nnote @ 'True'(X) ==> seen(X).\n"
    out['host-name-predicate'] = f'''!(let* (($space (new-space))
        ($posted (lang:run {chr_} $space {raw(named)}))
        ($echoed (lang:run {chr_} $space {raw(named_echo)})))
  (test (Named (check:status $posted) (check:status $echoed) (check:rows $space ("True" $x)) (check:rows $space (seen $y)))
        (Named Complete Complete (("True" 1)) ((seen 1)))))
'''
    for label, name, source_name in [('empty', '', "''"), ('nul', 'a\0b', r"'a\0b'")]:
        named_source = f":- chr_constraint {source_name}/1,seen/1. ?- {source_name}(1)."
        named_echo = f":- chr_constraint {source_name}/1,seen/1. note @ {source_name}(X) ==> seen(X)."
        out['host-predicate-' + label] = f'''!(let* (($space (new-space))
          ($posted (lang:run {chr_} $space {raw(named_source)}))
          ($echoed (lang:run {chr_} $space {raw(named_echo)})))
          (test (Named (check:status $posted) (check:status $echoed) (check:rows $space (seen $x)))
                (Named Complete Complete ((seen 1)))))
'''
    out['private-data-not-evaluated'] = '''(= (word $x) (add-atom &self (UnexpectedGuestEvaluation)))
''' + f'''!(let $answer (lang:eval {chr_} (new-space) {raw(":- chr_constraint word/1.\n?- word('7'),word('a b').\n")})
  (case $answer
    (((LangCompleteV1 $rows) (test (size-atom $rows) 2))
     ($other (println! (ExpectedCompleteGuestData $other))))))
!(test (check:rows &self (UnexpectedGuestEvaluation)) ())
'''
    symbols = ['7', 'true', 'false', 'a b', '$x', '&self', '!', 'λ']
    expected_records = json.dumps([{'expression': [{'symbol': 'word'}, {'symbol': name}]}
                                   for name in symbols], separators=(',', ':'), ensure_ascii=False)
    text = ":- chr_constraint word/1.\n?- " + ','.join("word('" + name + "')" for name in symbols) + '.\n'
    out['host-symbol-spellings-retained'] = f'''!(let* (($space (new-space))
        ($done (lang:run {chr_} $space {raw(text)}))
        ($view (langdef:capture-occurrences $space)))
  (case $view (((SourceOccurrenceViewV1 $handle)
    (test (langdef:occurrence-select $handle ("word"))
          (SourceOccurrencesV1 {raw(expected_records)}))))))
'''
    aliases = ':- chr_constraint word/1,seen/1. note @ word(X) ==> seen(X).'
    out['string-and-symbol-atom-inputs'] = f'''!(let* (($space (new-space))
          ($_ (add-atom $space (word "a"))) ($_ (add-atom $space (word a)))
          ($_ (add-atom $space ("word" "b")))
          ($done (lang:run {chr_} $space {raw(aliases)})))
       (test (Aliases (check:status $done) (check:count $space (word "a"))
                      (check:count $space (word a)) (check:count $space ("word" "b"))
                      (check:count $space (seen a)) (check:count $space (seen b)))
             (Aliases Complete 1 1 1 2 1)))
'''
    text_values = (":- chr_constraint word/1,kind/1. "
                   "text @ word('12') ==> kind(atom). "
                   "number @ word(12) ==> kind(integer).")
    out['numeric-string-keeps-atom-meaning'] = f'''!(let* (($space (new-space))
          ($_ (add-atom $space (word "12")))
          ($done (lang:run {chr_} $space {raw(text_values)})))
       (test (Kinds (check:status $done) (check:count $space (kind atom))
                    (check:count $space (kind integer)) (check:count $space (word "12")))
             (Kinds Complete 1 0 1)))
'''
    # Strings that need escapes on every crossing: the source reader, the IR wire, the
    # prepared machine program, publication and capture. A second program finds each
    # word by its exact spelling, so a changed character shows as a missing row.
    unusual = [("quote", "'a\"b'"), ("backslash", "'back\\\\slash'"), ("newline", "'line\\nbreak'"),
               ("tab", "'tab\\there'"), ("accent", "'é'"), ("wide", "'Ω日本'"), ("bell", "'bell\\a'")]
    unusual += [("hex", r"'\x3bb\'"), ("octal", r"'\141\'"),
                ("u4", r"'\u03bc'"), ("u8", r"'\U0001f600'"),
                ("openhex", r"'\x62'"), ("openoct", r"'\143'")]
    posted = ':- chr_constraint word/1.\n?- ' + ', '.join(f'word({atom})' for _, atom in unusual) + '.\n'
    finder = (':- chr_constraint word/1,found/1.\n'
              + ''.join(f'{label} @ word({atom}) ==> found({label}).\n' for label, atom in unusual))
    out['escaped-strings-roundtrip'] = f"""!(let* (($space (new-space))
        ($posted (lang:run {chr_} $space {raw(posted)}))
        ($found (lang:run {chr_} $space {raw(finder)})))
  (test (Escaped (check:status $posted) (check:status $found) (check:count $space (word $w))
                 (check:count $space (found $f)) (check:count $space (found quote)) (check:count $space (found newline))
                 (check:count $space (found wide)))
        (Escaped Complete Complete {len(unusual)} {len(unusual)} 1 1 1)))
"""
    charcodes = (r":- chr_constraint p/1,q/1. move @ p(X) <=> q(X). "
                 r"?- p(0'\x41\), p(0'\u03bb).")
    out['numeric-character-codes'] = f"!(test (lang:eval {chr_} {raw(charcodes)}) (LangCompleteV1 ((q 65) (q 955))))\n"
    invalid_scalar = r":- chr_constraint p/1. ?- p('\uD800')."
    out['invalid-scalar-retains-source'] = f"!(let (LangSourceErrorV1 (SourceDiagnosticV1 (SourceTextV1 $origin $kept) $reason)) {run(invalid_scalar)} (test $kept {raw(invalid_scalar)}))\n"
    overloaded_is = ":- chr_constraint p/1,is/1,is/3. go @ p(X) <=> is(X), is(X,X,X). ?- p(7)."
    out['builtin-name-other-arities'] = f"!(test (lang:eval {chr_} {raw(overloaded_is)}) (LangCompleteV1 ((is 7) (is 7 7 7))))\n"
    # Unicode names read as SWI reads them: lowercase and uncased letters start
    # atoms; an uppercase or titlecase start makes a variable.
    unicode_names = ':- chr_constraint w/1. ?- w(café), w(λ), w(漢字), w(même).'
    out['unicode-names'] = f"!(test (lang:eval {chr_} {raw(unicode_names)}) (LangCompleteV1 ((w café) (w λ) (w 漢字) (w même))))\n"
    unicode_variable = ':- chr_constraint w/1. ?- w(Été).'
    out['unicode-uppercase-is-variable'] = f"!(test (check:status {run(unicode_variable)}) Rejected)\n"
    unicode_rule = ':- chr_constraint w/1, v/1. copy @ w(Ω) ==> v(Ω). ?- w(ünï).'
    out['unicode-variable-in-rule'] = f"!(test (lang:eval {chr_} {raw(unicode_rule)}) (LangCompleteV1 ((w ünï) (v ünï))))\n"
    ideographic_space = ':- chr_constraint w/1. ?- w(a' + chr(0x3000) + 'b).'
    out['unicode-space-is-not-a-name'] = f"!(test (check:status {run(ideographic_space)}) ReadRejected)\n"
    # Double-quoted text is an atom (SWI's double_quotes=atom); other modes
    # need value kinds C0 does not have and are refused, not reinterpreted.
    dedupe = ':- chr_constraint w/1. dedupe @ w(X) BS w(X) <=> true. '.replace('BS', chr(92))
    quoted = dedupe + '?- w("abc"), w(abc), w("xyz").'
    out['double-quoted-atoms'] = f"!(test (lang:eval {chr_} {raw(quoted)}) (LangCompleteV1 ((w abc) (w xyz))))\n"
    flagged = ':- set_prolog_flag(double_quotes, atom). ' + dedupe + '?- w(' + '"abc"' + '), w(abc).'
    out['double-quotes-flag-atom'] = f"!(test (lang:eval {chr_} {raw(flagged)}) (LangCompleteV1 ((w abc))))\n"
    as_string = ':- set_prolog_flag(double_quotes, string). :- chr_constraint w/1. ?- w(' + '"abc"' + ').'
    out['double-quotes-flag-string-refused'] = f"!(test (check:status {run(as_string)}) Rejected)\n"
    return out


EXPECTED_ORDER = '((unrelated a) (minimum 2) (unrelated b) (unrelated c) (minimum 1 2))'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('work', type=Path)
    parser.add_argument('--case', action='append', dest='selected')
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    engine = Path(__file__).resolve().parents[2]
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    summary = {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'cases': [], 'failed': []}
    for name, body in cases(engine).items():
        if args.selected and name not in args.selected:
            continue
        body = body.replace('BODY', EXPECTED_ORDER)
        path = work / (name + '.metta')
        path.write_text(COMMON + body + f'!(println! (LangCasePassed {name}))\n')
        result = subprocess.run([binary, '--lang', 'petta', '--profile', 'extended', '--run-contract', path],
                                capture_output=True, timeout=600)
        (work / (name + '.out')).write_bytes(result.stdout)
        (work / (name + '.err')).write_bytes(result.stderr)
        passed = (result.stdout.count('✅'.encode()) == body.count('(test ') and result.returncode == 0
                  and all(line.startswith(b'run: status 0;') for line in result.stderr.splitlines())
                  and f'(LangCasePassed {name})'.encode() in result.stdout)
        (summary['cases'] if passed else summary['failed']).append(name)
        print(('PASS ' if passed else 'FAIL ') + name, flush=True)
    (work / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(f"(LangCHRSummary {len(summary['cases'])} passed {len(summary['failed'])} failed)")
    raise SystemExit(1 if summary['failed'] else 0)


if __name__ == '__main__':
    main()
