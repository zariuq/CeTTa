#!/usr/bin/env python3
"""Captured input heads preserve occurrences across later Space mutations."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('work', type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    args.work.mkdir(parents=True, exist_ok=False)
    common = '''!(import! &self langdef)
(= (view:owner (SourceInputViewV1 $owner)) $owner)
(= (view:error (Error $call $reason)) Rejected)
'''
    cases = {}
    cases['indexed-heads-all-arities'] = '''!(let* (($space (new-space))
 ($_ (view:noise $space 1000))
 ($_ (add-atom $space (Edge "a" "b")))
 ($_ (add-atom $space (Edge "a" "b")))
 ($_ (add-atom $space (Edge "bad-arity")))
 ($_ (add-atom $space Edge))
 ($view (view:owner (langdef:capture-input-view $space)))
 ($rows (langdef:input-view-rows $view ("Edge"))))
 (test (Selected $rows (langdef:input-view-record $view))
 (Selected ((Edge "a" "b") (Edge "a" "b") (Edge "bad-arity") Edge)
   (SourceInputViewRecordV1 1004 4 4))))
'''
    common += '''(= (view:noise $space $n)
 (if (== $n 0) ()
  (let $_ (add-atom $space (Noise $n)) (view:noise $space (- $n 1)))))
'''
    original = '''($_ (add-atom $space (Edge "a" "b")))
 ($_ (add-atom $space (Other "keep")))
 ($_ (add-atom $space (Edge "b" "c")))
 ($view (view:owner (langdef:capture-input-view $space)))'''
    mutations = {
        'append': '($_ (add-atom $space (Edge "late" "row")))',
        'remove': '($_ (remove-atom $space (Edge "a" "b")))',
        'replace': '''($_ (remove-atom $space (Edge "a" "b")))
 ($_ (add-atom $space (Edge "new" "row")))''',
        'clear': '($_ (collapse (match $space $row (remove-atom $space $row))))',
    }
    for name, mutation in mutations.items():
        cases['snapshot-' + name] = f'''!(let* (($space (new-space))
 {original}
 {mutation}
 ($rows (langdef:input-view-rows $view ("Edge")))
 ($again (langdef:input-view-rows $view ("Edge"))))
 (test (Snapshot $rows $again)
 (Snapshot ((Edge "a" "b") (Edge "b" "c")) ((Edge "a" "b") (Edge "b" "c")))))
'''
    cases['heads-are-a-set'] = f'''!(let* (($space (new-space))
 {original}
 ($rows (langdef:input-view-rows $view ("Other" "Edge" "Edge"))))
 (test $rows ((Edge "a" "b") (Other "keep") (Edge "b" "c"))))
'''
    cases['empty-and-missing-head'] = f'''!(let* (($space (new-space))
 {original})
 (test (Selections (langdef:input-view-rows $view ())
   (langdef:input-view-rows $view ("Absent"))) (Selections () ())))
'''
    cases['forged-and-invalid'] = '''!(test (view:error (langdef:input-view-rows
 (NativeHandle "cetta.source-input-view.v1" 1) ("Edge"))) Rejected)
!(test (view:error (langdef:capture-input-view 17)) Rejected)
!(let* (($space (new-space)) ($view (view:owner (langdef:capture-input-view $space))))
 (test (view:error (langdef:input-view-rows $view (Edge))) Rejected))
'''
    cases['no-evaluation'] = '''(= (Edge "a" "b") (add-atom &self (Unexpected)))
!(let* (($space (new-space))
 ($_ (add-atom $space (Edge "a" "b")))
 ($view (view:owner (langdef:capture-input-view $space))))
 (test (size-atom (langdef:input-view-rows $view ("Edge"))) 1))
!(test (collapse (match &self (Unexpected) yes)) ())
'''
    cases['set-space'] = '''!(let* (($space (new-space set))
 ($_ (add-atom $space (Edge "a" "b")))
 ($_ (add-atom $space (Edge "a" "b")))
 ($view (view:owner (langdef:capture-input-view $space)))
 ($_ (remove-atom $space (Edge "a" "b"))))
 (test (langdef:input-view-rows $view ("Edge")) ((Edge "a" "b"))))
'''
    cases['unsupported-layout-retains-fallback'] = '''!(let $space (new-space stack)
 (test (langdef:capture-input-view $space) SourceInputCopyV1))
'''
    results = []
    for name, body in cases.items():
        path = args.work / (name + '.metta')
        path.write_text(common + body + f'!(println! (InputViewCasePassed {name}))\n')
        result = subprocess.run([binary, '--lang', 'petta', '--profile', 'extended',
            '--run-contract', path], capture_output=True, timeout=60)
        (args.work / (name + '.out')).write_bytes(result.stdout)
        (args.work / (name + '.err')).write_bytes(result.stderr)
        diagnostics = re.sub(rb'run: status 0; queries [0-9]+; unsettled 0\n', b'', result.stderr)
        assert result.returncode == 0 and not diagnostics and result.stdout.count('✅'.encode()) == body.count('(test '), (name, result.returncode, result.stdout[-1800:], result.stderr[-1200:])
        assert f'(InputViewCasePassed {name})'.encode() in result.stdout, name
        results.append(name)
    (args.work / 'summary.json').write_text(json.dumps({
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'cases': results}, indent=2) + '\n')
    print(f'(SourceInputViewV1Summary {len(results)} passed)')


if __name__ == '__main__':
    main()
