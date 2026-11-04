#!/usr/bin/env python3
"""Qualify the public CeTTa lang library through real compiled D0 execution."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def quote(text):
    return json.dumps(text, ensure_ascii=False)


def raw(text):
    depth = 1
    while '"' + '#' * depth in text:
        depth += 1
    return 'r' + '#' * depth + '"' + text + '"' + '#' * depth


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
    source = (engine / 'tests/lang/reachability.dl').read_text()
    common = '''!(import! &self lang)
(= (check:status (LangCompleteV1 $facts)) Complete)
(= (check:status (LangPendingV1 $cursor)) Pending)
(= (check:status (LangSourceErrorV1 $message)) Rejected)
(= (check:status (LangSourceReadV1 $source $result)) ReadRejected)
(= (check:status (LangDefinitionUnadmittedV1 $module $reason)) Unadmitted)
(= (check:status (LangRunUnadmittedV1 $module $source)) RunUnadmitted)
(= (check:status (LangArgumentErrorV1 $expected $actual)) ArgumentRejected)
'''
    cases = {}
    def runlang(text=source, budget='1000', language='(lang:load Datalog)', destination='&self'):
        return f'(lang:run {language} {destination} {destination} {budget} {raw(text)})'
    edges = '(Edge "a" "b")\n(Edge "b" "c")\n'
    cases['reachability'] = edges + f'''!(test (check:status {runlang()}) Complete)
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 3)
!(test (match &self (Reach "a" "b") Present) Present)
!(test (match &self (Reach "b" "c") Present) Present)
!(test (match &self (Reach "a" "c") Present) Present)
!(test (size-atom (collapse (match &self (Edge $x $y) ($x $y)))) 2)
'''
    cases['set-occurrences'] = edges + '''(Edge "a" "b")
(Reach "a" "b")
(Reach "a" "b")
(Unrelated keep)
(Unrelated keep)
''' + f'''!(test (check:status {runlang()}) Complete)
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 4)
!(test (check:status {runlang()}) Complete)
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 4)
!(test (size-atom (collapse (match &self (Edge $x $y) ($x $y)))) 3)
!(test (size-atom (collapse (match &self (Unrelated $x) $x))) 2)
'''
    cases['cycle-and-multiple-proofs'] = '''(Edge "a" "b")
(Edge "b" "a")
(Edge "a" "a")
''' + f'''!(test (check:status {runlang()}) Complete)
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 4)
'''
    cases['one-shot-keeps-prior-materialization'] = edges + f'''!(test (check:status {runlang()}) Complete)
!(remove-atom &self (Edge "a" "b"))
!(remove-atom &self (Edge "b" "c"))
!(test {runlang()} (LangCompleteV1 ()))
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 3)
'''
    cases['empty'] = f'''!(test {runlang()} (LangCompleteV1 ()))
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
'''
    cases['empty-program'] = f'''!(test (lang:eval (lang:load Datalog) {raw('')}) (LangCompleteV1 ()))
'''
    cases['chosen-space'] = '(Edge "outside" "only")\n' + f'''!(let* (($space (new-space))
 ($_ (add-atom $space (Edge "a" "b")))
 ($done {runlang(destination='$space')})
 ($inside (collapse (match $space (Reach $x $y) ($x $y))))
 ($outside (collapse (match &self (Reach $x $y) ($x $y)))))
 (test (DestinationV1 (check:status $done) $inside $outside)
   (DestinationV1 Complete (("a" "b")) ())))
'''
    cases['unrelated-open-inputs'] = edges + '''(Other $unbound (nested $value))
(Reach $open "destination-only")
''' + f'''!(test (check:status {runlang()}) Complete)
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 4)
!(test (size-atom (collapse (match &self (Other $x $y) ($x $y)))) 1)
'''
    cases['resume-captured-inputs'] = edges + f'''!(let* (($pending {runlang(budget='0')})
 ($before (check:status $pending))
 ($_ (add-atom &self (Edge "c" "d")))
 ($again (lang:resume $pending 1))
 ($done (lang:resume $pending 1000)))
 (test (StatusesV1 $before (check:status $again) (check:status $done)) (StatusesV1 Pending Pending Complete)))
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 3)
!(test (collapse (match &self (Reach $x "d") $x)) ())
!(test (check:status {runlang()}) Complete)
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 6)
'''
    cases['resume-alias-no-republication'] = edges + f'''!(let* (($pending {runlang(budget='0')})
 ($done (lang:resume $pending 1000))
 ($_ (remove-atom &self (Reach "a" "c")))
 ($again (lang:resume $pending 1000)))
 (test (StatusesV1 (check:status $done) (check:status $again)) (StatusesV1 Complete Complete)))
!(test (collapse (match &self (Reach "a" "c") yes)) ())
'''
    cases['resume-alias-same-failure'] = '(Edge "a" $open)\n' + f'''!(let* (($pending {runlang(budget='0')})
 ($first (lang:resume $pending 1000))
 ($again (lang:resume $pending 1000)))
 (test (StatusesV1 (check:status $first) (check:status $again)) (StatusesV1 Rejected Rejected)))
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
'''
    cases['publication-unit-resume'] = edges + f'''!(let $pending {runlang(budget='0')}
 (let* (($run (check:advance $pending 2000))) (test $run Complete)))
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 3)
'''
    # Recursion is over the retained cursor; each publication consumes a unit.
    cases['publication-unit-resume'] = '''(= (check:advance $pending $left)
 (if (== $left 0) Exhausted
   (let $next (lang:resume $pending 1)
     (case $next (((LangCompleteV1 $facts) Complete)
       ((LangPendingV1 $cursor) (check:advance $next (- $left 1))) ($other Failed))))))
''' + cases['publication-unit-resume']
    comparisons = '''.decl Value(x:number)
.decl Positive(x:number)
.input Value
.output Positive
Positive(X) :- Value(X), X >= 0, X < 10.
'''
    cases['comparisons'] = '''(Value -1)
(Value 0)
(Value 5)
(Value 10)
''' + f'''!(test (check:status {runlang(comparisons)}) Complete)
!(test (size-atom (collapse (match &self (Positive $x) $x))) 2)
!(test (match &self (Positive 0) Present) Present)
!(test (match &self (Positive 5) Present) Present)
'''
    aliased = '''.decl Seed(x:symbol)
.input Seed
.output Seed
Seed("written").
'''
    cases['input-output-aliasing'] = '''(Seed "existing")
(Seed "existing")
''' + f'''!(test (check:status {runlang(aliased)}) Complete)
!(test (size-atom (collapse (match &self (Seed $x) $x))) 3)
!(test (check:status {runlang(aliased)}) Complete)
!(test (size-atom (collapse (match &self (Seed $x) $x))) 3)
'''
    for name, bad in [('arity', '(Edge "a")'), ('open', '(Edge "a" $unbound)'), ('type', '(Edge 3 "b")'), ('bare', 'Edge')]:
        cases['malformed-input-' + name] = ('!(add-atom &self Edge)' if name == 'bare' else bad) + f'''\n!(test (check:status {runlang()}) Rejected)
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
'''
    cases['symbol-text'] = '(Edge "日本語\\nline" "a\\\\b")\n' + f'''!(test (check:status {runlang()}) Complete)
!(test (collapse (match &self (Reach $x $y) ($x $y))) (("日本語\\nline" "a\\\\b")))
'''
    unsigned = '.decl U(x:unsigned)\n.decl Out(x:unsigned)\n.input U\n.output Out\nOut(X) :- U(X), X >= X.\n'
    cases['unsigned-width'] = '(U 18446744073709551615)\n' + f'''!(test (check:status {runlang(unsigned)}) Complete)
!(test (collapse (match &self (Out $x) $x)) (18446744073709551615))
'''
    badsource = '.decl X(x:symbol)\n.output X\nX($shared).\n'
    cases['invalid-source'] = f'''!(test (check:status {runlang(badsource)}) ReadRejected)
!(test (collapse (match &self (X $x) $x)) ())
'''
    cases['argument-errors'] = f'''!(test (check:status {runlang(budget='-1')}) ArgumentRejected)
!(test (check:status (lang:resume not-a-continuation 1)) ArgumentRejected)
!(test (check:status (lang:resume (LangPendingV1 (NativeHandle "cetta.source-cursor.v1" 1)) 1)) ArgumentRejected)
'''
    effects = '.decl Payload(x:symbol)\n.output Payload\nPayload("boom").\n'
    cases['no-host-execution'] = '''(= (Payload "boom") (add-atom &self (UnwantedEffect)))
''' + f'''!(test (check:status {runlang(effects)}) Complete)
!(test (collapse (match &self (Payload $x) $x)) ("boom"))
!(test (collapse (match &self (UnwantedEffect) yes)) ())
'''
    cases['source-file'] = edges + f'''!(test (check:status (lang:run (lang:load Datalog) &self
 (SourceFile {quote(str(engine / 'tests/lang/reachability.dl'))}))) Complete)
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 3)
'''
    cases['bad-destination'] = f'''!(test (check:status {runlang(destination='17')}) ArgumentRejected)
'''
    zero = '.decl Ready()\n.input Ready\n.output Ready\n'
    cases['zero-arity'] = '(Ready)\n' + f'''!(test (check:status {runlang(zero)}) Complete)
!(test (size-atom (collapse (match &self (Ready) present))) 1)
'''
    duplicate = '.decl Pair(x:symbol, y:symbol)\n.decl Same(x:symbol)\n.input Pair\n.output Same\nSame(X) :- Pair(X,X).\n'
    cases['repeated-variable'] = '(Pair "a" "b")\n(Pair "a" "a")\n(Pair "b" "b")\n' + f'''!(test (check:status {runlang(duplicate)}) Complete)
!(test (size-atom (collapse (match &self (Same $x) $x))) 2)
!(test (match &self (Same "a") present) present)
!(test (match &self (Same "b") present) present)
'''
    cases['symbol-nul-crlf'] = '(Edge ' + raw('a\x00b\r\nc') + ' "z")\n' + f'''!(test (check:status {runlang()}) Complete)
!(test (collapse (match &self (Reach $x $y) ($x $y))) (({raw('a'+chr(0)+'b'+chr(13)+chr(10)+'c')} "z")))
'''
    cases['symbol-cannot-inject-source'] = '!(let $x (str:string->symbol "\\"pretend\\"") (add-atom &self (Edge $x "b")))\n' + f'''!(test (check:status {runlang()}) Rejected)
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
'''
    bad_file=work/'bad-source.dl';bad_file.write_text('not a Datalog program')
    cases['source-file-origin']=f'''!(test (case (lang:run (lang:load Datalog) &self (SourceFile {quote(str(bad_file))}))
 (((LangSourceReadV1 (SourceTextV1 (SourceFile $path) $text) $why) $path))) {quote(str(bad_file))})
'''
    cases['parser-cutoff-resume'] = edges + f'''!(let* (($dl (lang:load Datalog))
 ($components (lang-internal:components $dl))
 ($rows (lang-internal:freeze-inputs (collapse (get-atoms &self))))
 ($cursor (langdef:source-cursor (LangCursorStateV1 (LangReadV1 (LangInvocationV1 $dl $components) (SourceTextV1 SourceInlineV1 {raw(source)}) (LangPublishV1 &self) (SourceInputRowsV1 $rows) 1 1 1 NoSourceForestV1))))
 ($pending (LangPendingV1 $cursor))
 ($first (lang:resume $pending 1))
 ($complete (lang:resume $pending 1000)))
 (test (StatusesV1 (check:status $first) (check:status $complete)) (StatusesV1 Pending Complete)))
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 3)
'''
    module_path = engine / 'langdef/lang/Datalog.module'
    for name, language in [
        ('package-file', f'(lang:load {quote(str(module_path))})'),
        ('package-memory', '(lang:def (lang:module (lang:load Datalog)))')]:
        cases[name] = edges + f'''!(test (check:status {runlang(language=language)}) Complete)
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 3)
'''
    evaluate = f'(lang:eval (lang:load Datalog) {raw(source)})'
    cases['isolated-default'] = edges + f'''!(test (case {evaluate}
 (((LangCompleteV1 $facts) (size-atom $facts)))) 3)
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
!(test (size-atom (collapse (match &self (Edge $x $y) ($x $y)))) 2)
'''
    cases['source-last-default'] = edges + f'''!(test (check:status (lang:run (lang:load Datalog) {raw(source)})) Complete)
!(test (size-atom (collapse (match &self (Reach $x $y) ($x $y)))) 3)
'''
    cases['separate-input-output'] = '(Edge "host" "only")\n' + f'''!(let* (($input (new-space))
 ($output (new-space))
 ($_ (add-atom $input (Edge "a" "b")))
 ($_ (add-atom $output (Edge "not" "input")))
 ($_ (add-atom $output (Reach "a" "b")))
 ($_ (add-atom $output (Reach "a" "b")))
 ($_ (add-atom $output (Unrelated keep)))
 ($done (lang:run (lang:load Datalog) $input $output {raw(source)})))
 (test (Separate (check:status $done)
   (collapse (match $output (Reach $x $y) ($x $y)))
   (collapse (match $input (Reach $x $y) ($x $y)))
   (collapse (match &self (Reach $x $y) ($x $y)))
   (collapse (match $output (Unrelated $x) $x)))
  (Separate Complete (("a" "b") ("a" "b")) () () (keep))))
'''
    cases['isolated-capture-resume'] = edges + f'''!(let* (($pending (lang:eval (lang:load Datalog) &self 0 {raw(source)}))
 ($_ (remove-atom &self (Edge "a" "b")))
 ($_ (add-atom &self (Edge "c" "d")))
 ($done (lang:resume $pending 1000))
 ($again (lang:resume $pending 1000)))
 (test (Isolated (check:status $done) (== $done $again)
   (case $done (((LangCompleteV1 $facts) (size-atom $facts))))
   (collapse (match &self (Reach $x $y) ($x $y))))
   (Isolated Complete True 3 ())))
'''
    cases['isolated-unit-resume'] = '''(= (check:finish $result $left)
 (case $result
   (((LangCompleteV1 $facts) (size-atom $facts))
    ((LangPendingV1 $cursor) (if (> $left 0)
      (check:finish (lang:resume $result 1) (- $left 1)) Exhausted))
    ($other Failed))))
''' + edges + f'''!(test (check:finish (lang:eval (lang:load Datalog) &self 0 {raw(source)}) 2000) 3)
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
'''
    cases['isolated-malformed-input'] = '(Edge "a")\n' + f'''!(test (check:status {evaluate}) Rejected)
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
'''
    cases['isolated-data-only'] = '''(= (Reach $x $y) (add-atom &self (UnwantedEffect)))
''' + edges + f'''!(test (case {evaluate} (((LangCompleteV1 $facts) (size-atom $facts)))) 3)
!(test (collapse (match &self (UnwantedEffect) yes)) ())
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
'''
    for name, language in [('file', f'(lang:load {quote(str(module_path))})'),
                           ('memory', '(lang:def (lang:module (lang:load Datalog)))')]:
        cases['isolated-package-' + name] = edges + f'''!(test (case (lang:eval {language} {raw(source)})
 (((LangCompleteV1 $facts) (size-atom $facts)))) 3)
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
'''
    cases['source-last-space-errors'] = f'''!(test (check:status (lang:eval (lang:load Datalog) 17 {raw(source)})) ArgumentRejected)
!(test (check:status (lang:run (lang:load Datalog) &self 17 {raw(source)})) ArgumentRejected)
'''
    for name, bad in [('arity', '(Edge "bad")'), ('bare', 'Edge'), ('open', '(Edge "a" $open)')]:
        cases['captured-malformed-removed-' + name] = f'''!(let* (($input (new-space))
 ($_ (add-atom $input {bad}))
 ($pending (lang:eval (lang:load Datalog) $input 0 {raw(source)}))
 ($_ (collapse (match $input $row (remove-atom $input $row)))))
 (test (check:status (lang:resume $pending 1000)) Rejected))
'''
    cases['separate-resume'] = f'''!(let* (($input (new-space)) ($output (new-space))
 ($_ (add-atom $input (Edge "a" "b")))
 ($pending (lang:run (lang:load Datalog) $input $output 0 {raw(source)}))
 ($_ (remove-atom $input (Edge "a" "b")))
 ($_ (add-atom $input (Edge "late" "input")))
 ($_ (add-atom $output (Reach "a" "b")))
 ($_ (add-atom $output (Reach "a" "b")))
 ($done (lang:resume $pending 1000)))
 (test (Result (check:status $done)
  (collapse (match $output (Reach $x $y) ($x $y)))
  (collapse (match $input (Reach $x $y) ($x $y))))
  (Result Complete (("a" "b") ("a" "b")) ())))
'''
    cases['copied-layout-resume'] = f'''!(let* (($input (new-space stack))
 ($_ (add-atom $input (Edge "a" "b")))
 ($pending (lang:eval (lang:load Datalog) $input 0 {raw(source)}))
 ($_ (remove-atom $input (Edge "a" "b")))
 ($done (lang:resume $pending 1000)))
 (test $done (LangCompleteV1 ((Reach "a" "b")))))
'''
    summary = {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), 'cases': []}
    for name, body in cases.items():
        if args.selected and name not in args.selected:
            continue
        path = work / (name + '.metta')
        path.write_text(common + body + f'!(println! (LangCasePassed {name}))\n')
        result = subprocess.run([binary, '--lang', 'petta', '--profile', 'extended', '--run-contract', path],
                                capture_output=True, timeout=120)
        (work / (name + '.out')).write_bytes(result.stdout)
        (work / (name + '.err')).write_bytes(result.stderr)
        assert result.stdout.count('✅'.encode()) == body.count('(test ') and result.returncode == 0 and all(line.startswith(b'run: status 0;') for line in result.stderr.splitlines()) and f'(LangCasePassed {name})'.encode() in result.stdout, (name, result.returncode, result.stdout[-1500:], result.stderr[-1500:])
        summary['cases'].append(name)
    (work / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(f"(LangLibraryV1Summary {len(summary['cases'])} passed)")

if __name__ == '__main__':
    main()
