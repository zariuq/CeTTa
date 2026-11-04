#!/usr/bin/env python3
"""A native parse outcome cannot substitute for independent source coverage."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from check_lang_library import raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('work', type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    args.work.mkdir(parents=True, exist_ok=False)
    source = '.decl Result(x:number)\n.output Result\nResult(1).\n'
    common = '''!(import! &self lang)
(= (verification:status $reply)
 (case (lang-internal:reply $reply)
   (((LangReplyV1 $object) (lang-internal:json-field $object "status"))
    ((LangSourceErrorV1 $why) "rejected") ($other (Unexpected $other)))))
(= (verification:check $compiler $source $catalogue $forest $identity $rounds)
 (let $request (LCons $source (LCons (repr $catalogue) (LCons $forest (LCons $identity (LCons $rounds LNil)))))
  (verification:status (langdef:source-compiler-call $compiler "verify" $request))))
'''
    # Each case obtains evidence from the real loaded parser. The altered
    # catalogue/source/identity is supplied to the independent verifier only.
    checks = {
        'complete': '(verification:check $compiler $source $values $forest-wire $identity-wire "4096") "verified"',
        'pending': '(verification:check $compiler $source $values $forest-wire $identity-wire "0") "pending"',
        'missing-result': '(verification:check $compiler $source (SourceValuesV1) $forest-wire $identity-wire "4096") "rejected"',
        'duplicate-result': '(verification:check $compiler $source (SourceValuesV1 $packet $packet) $forest-wire $identity-wire "4096") "rejected"',
        'different-source': '(verification:check $compiler "not the captured source" $values $forest-wire $identity-wire "4096") "rejected"',
        'bad-forest': '(verification:check $compiler $source $values "00" $identity-wire "4096") "rejected"',
        'bad-identity': '(verification:check $compiler $source $values $forest-wire "00" "4096") "rejected"',
        'bad-packet': '(verification:check $compiler $source (SourceValuesV1 forged) $forest-wire $identity-wire "4096") "rejected"',
        'bad-allowance': '(verification:check $compiler $source $values $forest-wire $identity-wire "-1") "rejected"',
    }
    results = []
    for name, check in checks.items():
        body = f'''!(let $source {raw(source)}
 (case (lang-internal:components (lang:load Datalog))
   (((LangComponentsV1 $module $parser $compiler $metadata $execution)
     (case (langdef:parse-source-plan $parser $source 1000000 100)
       (((SourceParseV1 0 $accepted $values $forest)
         (case $values
           (((SourceValuesV1 $packet)
             (case (langdef:source-plan-evidence $forest)
               (((SourceParserEvidenceV1 $forest-wire $identity-wire)
                  (test {check}))))))))))))))
'''
        path = args.work / (name + '.metta')
        path.write_text(common + body + f'!(println! (SourceVerificationPassed {name}))\n')
        result = subprocess.run([binary, '--lang', 'petta', '--profile', 'extended',
            '--run-contract', path], capture_output=True, timeout=120)
        (args.work / (name + '.out')).write_bytes(result.stdout)
        (args.work / (name + '.err')).write_bytes(result.stderr)
        diagnostic = re.sub(rb'run: status 0; queries [0-9]+; unsettled 0\n', b'', result.stderr)
        assert result.returncode == 0 and not diagnostic and result.stdout.count('✅'.encode()) == 1, (name, result.returncode, result.stdout[-1600:], result.stderr[-1200:])
        assert f'(SourceVerificationPassed {name})'.encode() in result.stdout, name
        results.append(name)
    (args.work / 'summary.json').write_text(json.dumps({
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'cases': results}, indent=2) + '\n')
    print(f'(LangSourceVerificationV1Summary {len(results)} passed)')


if __name__ == '__main__':
    main()
