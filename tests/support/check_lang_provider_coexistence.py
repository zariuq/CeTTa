#!/usr/bin/env python3
"""Load custom-built Datalog and packaged CHR providers in both orders."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('work', type=Path)
    parser.add_argument('--lean-root', required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    engine = Path(__file__).resolve().parents[2]
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    packages = [engine / 'langdef/lang/Datalog.module', engine / 'langdef/lang/CHR.module']
    before = {str(path): digest(path) for path in packages}
    provider, package = work / 'custom-datalog.so', work / 'CustomDatalog.module'
    with (work / 'build.log').open('w') as log:
        subprocess.run([sys.executable, str(engine / 'tools/build_datalog_source_compiler.py'),
                        '--lean-root', str(args.lean_root.resolve(strict=True)), '--skip-build',
                        '--output', str(provider), '--package-output', str(package)],
                       cwd=engine, stdout=log, stderr=subprocess.STDOUT, check=True)
    unchanged = all(digest(Path(path)) == value for path, value in before.items())
    assert unchanged, 'custom output must not rewrite the packaged language definitions'
    expressions = {'datalog': '(lang:load ' + json.dumps(str(package)) + ')', 'chr': '(lang:load CHR)'}
    records = []
    for order in [('datalog', 'chr'), ('chr', 'datalog')]:
        name = '-'.join(order)
        bindings = ' '.join('($' + key + ' ' + expressions[key] + ')' for key in order)
        source = '''!(import! &self lang)
!(let* (BINDINGS)
   (test (Results
           (lang:eval $datalog (new-space) r#".decl Marker(x:number)
.output Marker
Marker(5).
"#)
           (lang:eval $chr (new-space) r#":- chr_constraint p/1.
?- p(1).
"#))
         (Results (LangCompleteV1 ((Marker 5))) (LangCompleteV1 ((p 1))))))
!(println! ProviderCoexistencePassed)
'''.replace('BINDINGS', bindings)
        program = work / (name + '.metta')
        program.write_text(source)
        run = subprocess.run([binary, '--lang', 'petta', '--profile', 'extended',
                              '--run-contract', program], capture_output=True, timeout=180)
        (work / (name + '.out')).write_bytes(run.stdout)
        (work / (name + '.err')).write_bytes(run.stderr)
        passed = (run.returncode == 0 and run.stdout.count('✅'.encode()) == 1 and
                  b'ProviderCoexistencePassed' in run.stdout and
                  all(line.startswith(b'run: status 0;') for line in run.stderr.splitlines()))
        records.append({'order': list(order), 'returncode': run.returncode, 'passed': passed})
        print(('PASS ' if passed else 'FAIL ') + name, flush=True)
    summary = {'binary_sha256': digest(binary), 'provider_sha256': digest(provider),
               'packaged_definitions_unchanged': unchanged, 'cases': records,
               'all_passed': unchanged and all(record['passed'] for record in records)}
    (work / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    raise SystemExit(0 if summary['all_passed'] else 1)


if __name__ == '__main__':
    main()
