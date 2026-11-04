#!/usr/bin/env python3
"""Check source-provider ownership and native replies independently of D0."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('binary',type=Path);p.add_argument('provider',type=Path);p.add_argument('work',type=Path)
    a=p.parse_args();a.work.mkdir(parents=True,exist_ok=False)
    path=a.work/'provider.metta';provider=json.dumps(str(a.provider.resolve(strict=True)))
    counted='r#"a'+chr(0)+'b\r\n"#'
    source=f'''!(import! &self (library langdef))
(= (source-test:error (Error $call $reason)) ExpectedError)
!(let $p (langdef:load-source-compiler {provider})
 (let* (($reply (langdef:source-compiler-call $p "echo" (LCons "日本語" LNil)))
        ($counted (langdef:source-compiler-call $p "echo" (LCons {counted} LNil)))
        ($failed (source-test:error (langdef:source-compiler-call $p "failure-buffer" LNil)))
        ($bad (source-test:error (langdef:source-compiler-call $p "invalid-utf8" LNil)))
        ($closed (langdef:close-source-compiler $p))
        ($stale (source-test:error (langdef:source-compiler-call $p "echo" (LCons "gone" LNil)))))
   (test (ProviderResultsV1 $reply $counted $failed $bad $stale)
     (ProviderResultsV1 "日本語" {counted} ExpectedError ExpectedError ExpectedError))))
!(test (source-test:error (langdef:source-compiler-call (NativeHandle "cetta.source-compiler.v1" 1) "echo" (LCons "forged" LNil))) ExpectedError)
!(println! ProviderAbiPassed)
'''
    path.write_text(source)
    r=subprocess.run([a.binary.resolve(strict=True),'--lang','petta','--profile','extended','--run-contract',path.resolve()],capture_output=True,timeout=60)
    (a.work/'out').write_bytes(r.stdout);(a.work/'err').write_bytes(r.stderr)
    assert r.returncode==0 and r.stdout.count('✅'.encode())==2 and b'ProviderAbiPassed' in r.stdout,(r.returncode,r.stdout[-2000:],r.stderr[-2000:])
    for dialect in ('petta','he'):
        dialect_path=a.work/(dialect+'-data.metta')
        dialect_path.write_text(f'''!(import! &self langdef)
!(if (== (langdef:execution-profile) "extended")
  (if (langdef:natural-budget? 1)
    (if (langdef:natural-budget? -1) (println! UnexpectedNegativeBudget)
      (println! CompilerProfilePassed))
    (println! UnexpectedBudget))
  (println! UnexpectedProfile))
!(let $p (langdef:load-source-compiler {provider})
 (let $reply (langdef:source-compiler-call $p "echo" (LCons {counted} LNil))
  (if (== $reply {counted}) (println! CompilerDataPassed) (println! UnexpectedData))))
!(let $cursor (langdef:source-cursor (CursorDataV1 Before))
 (let* (($held (langdef:source-cursor-state $cursor))
        ($_ (langdef:replace-source-cursor $cursor (CursorDataV1 After)))
        ($now (langdef:source-cursor-state $cursor)))
   (if (== ($held $now) ((CursorDataV1 Before) (CursorDataV1 After)))
     (println! CursorValuePassed) (println! UnexpectedCursor))))
''')
        r=subprocess.run([a.binary.resolve(strict=True),'--lang',dialect,'--profile','extended','--run-contract',dialect_path.resolve()],capture_output=True,timeout=60)
        (a.work/(dialect+'-data.out')).write_bytes(r.stdout)
        (a.work/(dialect+'-data.err')).write_bytes(r.stderr)
        assert r.returncode==0 and r.stdout.count(b'CompilerProfilePassed')==1 and r.stdout.count(b'CompilerDataPassed')==1 and r.stdout.count(b'CursorValuePassed')==1 and b'Unexpected' not in r.stdout,(dialect,r.returncode,r.stdout[-2000:],r.stderr[-2000:])
    print('(SourceCompilerAbiV1Summary ownership malformed-utf8 partial-failure closed forged; HE/PeTTa data passed)')

if __name__=='__main__':main()
