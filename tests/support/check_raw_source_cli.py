#!/usr/bin/env python3
"""Exercise extended raw regions through the actual document and term readers."""

import argparse
from pathlib import Path
import subprocess


def ordinary(body):
    return b'"' + body.replace(b'\\', b'\\\\').replace(b'"', b'\\"').replace(
        b'\n', b'\\n').replace(b'\r', b'\\r').replace(b'\t', b'\\t') + b'"'


def raw(body, minimum=1):
    depth = minimum
    while b'"' + b'#' * depth in body:
        depth += 1
    return b'r' + b'#' * depth + b'"' + body + b'"' + b'#' * depth


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("binary", type=Path)
    p.add_argument("work", type=Path)
    args = p.parse_args()
    binary = args.binary.resolve()
    args.work.mkdir(parents=True, exist_ok=False)
    cases = [b'', b'quotes " and \\n remain literal', b') ; guest comment\n(',
             b'r#"nested"# and r##"nested again"##', b'\r\n  indented\tcode\r\n',
             '∀日本語🌱'.encode(), b'a\x00b\x01c', b'"###', b'$shared &self !(execute-me)']
    forms = []
    for index, body in enumerate(cases):
        forms.append(b'!(if (== ' + raw(body) + b' ' + ordinary(body) +
                     b') (println! (RawRegionPass ' + str(index).encode() +
                     b')) (println! (RawRegionFail ' + str(index).encode() + b')))\n')
    # The same raw literal inside a list exercises the list dispatch and the
    # document splitter; semicolons and unmatched parentheses belong to data.
    forms.append(b'!(if (== [' + raw(b'); \n quote "') + b'] [' + ordinary(b'); \n quote "') +
                 b']) (println! (RawRegionPass list)) (println! (RawRegionFail list)))\n')
    forms.append(b'!(if (== r#"same"#r#"same"#) (println! (RawRegionPass adjacent)) (println! (RawRegionFail adjacent)))\n')
    forms.append(b'!(if (== "same"r#"same"#) (println! (RawRegionPass adjacent-string)) (println! (RawRegionFail adjacent-string)))\n')
    source = args.work / 'source.metta'
    source.write_bytes(b''.join(forms))
    for dialect in ('he', 'petta'):
        result = subprocess.run([binary, '--lang', dialect, '--profile', 'extended', source],
                                capture_output=True, timeout=60)
        (args.work / f'{dialect}.out').write_bytes(result.stdout)
        (args.work / f'{dialect}.err').write_bytes(result.stderr)
        assert result.returncode == 0, (dialect, result.stderr)
        assert b'RawRegionFail' not in result.stdout, (dialect, result.stdout)
        assert result.stdout.count(b'RawRegionPass') == len(forms), (dialect, result.stdout)
        for index, bad in enumerate((b'!(println! r#"unfinished)', b'!(println! r##"wrong"#)',
                                     b'!(println! r#"\xff"#)')):
            path = args.work / f'bad-{index}.metta'
            path.write_bytes(bad)
            rejected = subprocess.run([binary, '--lang', dialect, '--profile', 'extended', path],
                                      capture_output=True, timeout=60)
            assert rejected.returncode != 0, (dialect, bad, rejected.stdout)
            assert b'raw source region' in rejected.stderr, (dialect, rejected.stderr)
    print(f'RawSourceCLI: {len(forms) * 2} successful controls, 6 malformed/incomplete controls')


if __name__ == '__main__':
    main()
