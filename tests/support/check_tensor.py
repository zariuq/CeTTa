#!/usr/bin/env python3
"""Public tensor controls across the three delivery policies."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    args = parser.parse_args()
    dense = '(tensor:from-values f64 (2 3) (1.0 2.0 3.0 4.0 5.0 6.0))'
    sparse = '(tensor:from-coo f64 (2 3) (((1 0) 0.0) ((0 1) 2.0)))'
    cases = [
        (f'(tensor:inspect {dense})', '(TensorData f64 (2 3) ((0 1.0) (1 2.0) (2 3.0) (3 4.0) (4 5.0) (5 6.0)))'),
        (f'(tensor:shape {dense})', '(2 3)'),
        (f'(tensor:dtype {dense})', 'f64'),
        (f'(let $value {dense} (get-type $value))', 'Tensor'),
        (f'(tensor:sum {dense})', '21.0'),
        (f'(tensor:sum-reference {dense})', '21.0'),
        (f'(tensor:sum-by {dense} tensor-test:square)', '91.0'),
        (f'(tensor:sum (tensor:map-stored {dense} tensor-test:square))', '91.0'),
        (f'(tensor:at {dense} (1 2))', '(tensor:present 6.0)'),
        (f'(tensor:at {sparse} (1 0))', '(tensor:present 0.0)'),
        (f'(tensor:at {sparse} (0 0))', 'tensor:absent'),
        ('(tensor:inspect (tensor:where (tensor:from-values bool (2) (True False)) '
         '(tensor:from-values i64 (2) (7 8)) (tensor:from-values i64 (2) (3 4))))',
         '(TensorData i64 (2) ((0 7) (1 4)))'),
        ('(tensor:inspect (tensor:where (tensor:from-coo bool (3) ((0 True) (1 False))) '
         '(tensor:from-coo f64 (3) ((0 -0.0))) (tensor:from-coo f64 (3) ((0 7.0) (2 8.0)))))',
         '(TensorData f64 (3) ((0 -0.0)))'),
        ('(tensor:inspect (tensor:where (tensor:from-values bool (0) ()) '
         '(tensor:from-values i64 (0) ()) (tensor:from-values i64 (0) ()) ))',
         '(TensorData i64 (0) ())'),
        ('(tensor:where (tensor:from-values bool (2) (True False)) '
         '(tensor:from-values i64 (1 2) (7 8)) (tensor:from-values i64 (2) (3 4)))',
         '(Tensor:Fault shape-mismatch)'),
        ('(tensor:where (tensor:from-values bool () (True)) '
         '(tensor:from-values i64 () (7)) (tensor:from-values f64 () (3.0)))',
         '(Tensor:Fault unsupported-dtype)'),
        ('(tensor:where Unknown Unknown Unknown)', '(tensor:where Unknown Unknown Unknown)'),
        (f'(tensor:at {sparse} (2 0))', '(Tensor:Fault index-out-of-bounds)'),
        (f'(tensor:convert {sparse} dense)', '(Tensor:Fault shape-mismatch)'),
        (f'(tensor:encode {sparse})', '(Tensor:Fault invalid-encoding)'),
        (f'(tensor:inspect (tensor:matvec {dense} (tensor:from-values f64 (3) (2.0 1.0 -1.0))))', '(TensorData f64 (2) ((0 1.0) (1 7.0)))'),
        (f'(tensor:inspect (tensor:matvec (tensor:convert {sparse} csr) (tensor:from-values f64 (3) (2.0 1.0 -1.0))))', '(TensorData f64 (2) ((0 2.0) (1 0.0)))'),
        (f'(tensor:inspect (tensor:view {dense} (3 2) (1 3) 0))', '(TensorData f64 (3 2) ((0 1.0) (1 4.0) (2 2.0) (3 5.0) (4 3.0) (5 6.0)))'),
        (f'(tensor:inspect (tensor:view {dense} (3) (-1) 2))', '(TensorData f64 (3) ((0 3.0) (1 2.0) (2 1.0)))'),
        (f'(tensor:inspect (tensor:view {dense} (3) (0) 2))', '(TensorData f64 (3) ((0 3.0) (1 3.0) (2 3.0)))'),
        (f'(let $value {dense} (== $value (tensor:convert (tensor:convert $value csc) dense)))', 'True'),
        (f'(let $value {dense} (== $value (tensor:decode (tensor:encode $value))))', 'True'),
        (f'(let $value {dense} (== $value (tensor:copy $value)))', 'True'),
        ('(tensor:sum (tensor:from-values i64 (2) (9223372036854775807 1)))', '(Tensor:Fault integer-overflow)'),
        ('(tensor:from-values u8 (1) (256))', '(Tensor:Fault integer-overflow)'),
        ('(tensor:inspect (tensor:from-values bool (3) (True False True)))', '(TensorData bool (3) ((0 True) (1 False) (2 True)))'),
        ('(tensor:inspect (tensor:from-values f64 () (7.0)))', '(TensorData f64 () ((0 7.0)))'),
        ('(tensor:inspect (tensor:from-values f64 (2 0) ()))', '(TensorData f64 (2 0) ())'),
        ('(tensor:inspect (tensor:top-k (tensor:from-values f64 (6) (2.0 4.0 4.0 -1.0 8.0 4.0)) 3))', '(TensorData index (3) ((0 4) (1 1) (2 2)))'),
        ('(tensor:from-coo i64 (1) ((0 2) (0 5)))', '(Tensor:Fault duplicate-coordinate)'),
        ('(tensor:at (tensor:from-coo i64 (1) ((0 2) (0 5)) first) 0)', '(tensor:present 2)'),
        ('(tensor:at (tensor:from-coo i64 (1) ((0 2) (0 5)) last) 0)', '(tensor:present 5)'),
        ('(tensor:at (tensor:from-coo i64 (1) ((0 2) (0 5)) sum) 0)', '(tensor:present 7)'),
        ('(tensor:inspect (tensor:from-coo i64 (2) ((1 7) ((0) 9) ((1) 3) (1 2)) (combine tensor-test:subtract)))', '(TensorData i64 (2) ((0 9) (1 2)))'),
        ('(tensor:from-coo u8 (1) ((0 255) (0 1)) (combine +))', '(Tensor:Fault integer-overflow)'),
        ('(tensor:from-coo i64 (1) ((0 1) (1 2)) (combine +))', '(Tensor:Fault index-out-of-bounds)'),
        ('(tensor:inspect (tensor:from-coo f32 (1) ((0 16777216.0) (0 1.0) (0 -16777216.0)) (combine +)))', '(TensorData f32 (1) ((0 0.0)))'),
        ('''(let* (($builder (tensor:builder f64 (2 3)))
              ($alias $builder) ($added (tensor:put $builder (1 1) 8.0))
              ($value (tensor:freeze $builder)) ($invalid (tensor:put $alias 0 3.0))
              ($closed (tensor:close $alias)))
          (Builder (tensor:inspect $value) $invalid (tensor:freeze $builder)))''',
         '(Builder (TensorData f64 (2 3) ((4 8.0))) (Tensor:Fault frozen-builder) (Tensor:Fault closed-or-forged-builder))'),
        ('(tensor:freeze (NativeHandle "tensor-builder-v2" 1))', '(Tensor:Fault closed-or-forged-builder)'),
        ('''(let* (($builder (tensor:builder i64 (2) (combine tensor-test:subtract)))
              ($alias $builder) ($one (tensor:put $builder 0 9)) ($two (tensor:put $builder 0 4))
              ($value (tensor:freeze $builder)) ($after (tensor:put $alias 1 8))
              ($again (tensor:freeze $alias)) ($closed (tensor:close $builder)))
          (CombinedBuilder (tensor:inspect $value) $after $again))''',
         '(CombinedBuilder (TensorData i64 (2) ((0 5))) (Tensor:Fault frozen-builder) (Tensor:Fault frozen-builder))'),
        ('(tensor:sum Custom)', '23'),
        ('(tensor:sum unrecognized)', '(tensor:sum unrecognized)'),
        ('(tensor:all-finite (tensor:from-values f64 (2) (1.0 2.0)))', 'True'),
        ('(tensor:inspect (tensor:normalize (tensor:from-values f64 (2) (3.0 4.0))))', '(TensorData f64 (2) ((0 0.6) (1 0.8)))'),
        ('(tensor:normalize (tensor:from-values f64 (2) (0.0 0.0)))', '(Tensor:Fault zero-norm)'),
        ('''(let* (($source (tensor:from-values f64 (2 2) (1.0 0.0 0.0 1.0)))
             ($index (tensor:cosine-index $source))
             ($seen (tensor:index-source $index))
             ($scores (tensor:cosine-scores $index (tensor:from-values f64 (2) (1.0 0.0))))
             ($closed (tensor:close $index)))
           (Owner (== $source $seen) (tensor:inspect $scores) (tensor:index-source $index)))''',
         '(Owner True (TensorData f64 (2) ((0 1.0) (1 0.0))) (Tensor:Fault closed-or-forged-index))'),
        (r'(tensor:inspect (tensor:from-json f64 (2 2) "{\"v\":[[1,2],[-3.5,4]],\"ignored\":\"λ\"}" ("v")))', '(TensorData f64 (2 2) ((0 1.0) (1 2.0) (2 -3.5) (3 4.0)))'),
        ('(tensor:inspect (tensor:from-json i64 (2) "[9007199254740993,-9223372036854775808]" ()))', '(TensorData i64 (2) ((0 9007199254740993) (1 -9223372036854775808)))'),
        (r'(tensor:from-json f64 (1) "{\"missing\":[]}" ("v"))', 'JsonMissingV1'),
        ('(case (tensor:from-json f64 (2 2) "[[1,2],[3]]" ()) (((Tensor:Fault typed-json $reason) rejected) ($other accepted)))', 'rejected'),
        (r'(case (tensor:from-json f64 (2) "{\"v\":[1,2],\"bad\":[}" ("v")) (((Tensor:Fault typed-json $reason) rejected) ($other accepted)))', 'rejected'),
        ('(case (tensor:from-json f64 (1) "[1e999]" ()) (((Tensor:Fault typed-json $reason) rejected) ($other accepted)))', 'rejected'),
        ('(tensor:inspect (tensor:from-json bool (2) "[true,false]" ()))', '(TensorData bool (2) ((0 True) (1 False)))'),
        ('(tensor:inspect (tensor:from-json bool () "true" ()))', '(TensorData bool () ((0 True)))'),
        ('(tensor:inspect (tensor:from-json index (2) "[-0,1]" ()))', '(TensorData index (2) ((0 0) (1 1)))'),
        ('(tensor:inspect (tensor:promote (tensor:from-values u8 (2) (0 255)) f64))', '(TensorData f64 (2) ((0 0.0) (1 255.0)))'),
        ('(tensor:promote (tensor:from-values i64 (1) (7)) f64)', '(Tensor:Fault unsupported-promotion)'),
        ('(tensor:inspect (tensor:cast (tensor:from-values i64 (1) (7)) f64))', '(TensorData f64 (1) ((0 7.0)))'),
        ('(tensor:cast (tensor:from-values i64 (1) (9007199254740993)) f64)', '(Tensor:Fault inexact-cast)'),
        ('(tensor:inspect (tensor:cast (tensor:from-values i64 (1) (9007199254740993)) f64 nearest))', '(TensorData f64 (1) ((0 9007199254740992.0)))'),
        ('(tensor:cast (tensor:from-values f64 (1) (1.25)) i64)', '(Tensor:Fault inexact-cast)'),
        ('(tensor:inspect (tensor:cast (tensor:from-values f64 (2) (-1.25 2.9)) i64 truncate))', '(TensorData i64 (2) ((0 -1) (1 2)))'),
        ('(tensor:cast (tensor:from-values i64 (1) (-1)) index)', '(Tensor:Fault integer-overflow)'),
        ('(tensor:inspect (tensor:cast (tensor:from-coo i64 (3) ((1 0))) f32))', '(TensorData f32 (3) ((1 0.0)))'),
        ('(tensor:cast (tensor:from-values f64 (1) (0.1)) f32)', '(Tensor:Fault inexact-cast)'),
        ('(tensor:inspect (tensor:from-coordinates (2 3) (tensor:from-json index (2) "[3,1]" ()) (tensor:from-json f64 (2) "[0.0,2.0]" ())))', '(TensorData f64 (2 3) ((1 2.0) (3 0.0)))'),
        ('(tensor:inspect (tensor:from-coordinates (2) (tensor:from-values index (3) (1 0 1)) (tensor:from-values i64 (3) (7 2 3)) sum))', '(TensorData i64 (2) ((0 2) (1 10)))'),
        ('(tensor:inspect (tensor:from-coordinates (2) (tensor:from-values index (3) (1 0 1)) (tensor:from-values i64 (3) (7 2 3)) (combine tensor-test:subtract)))', '(TensorData i64 (2) ((0 2) (1 4)))'),
        ('(tensor:from-coordinates (2) (tensor:from-values index (1) (2)) (tensor:from-values i64 (1) (1)))', '(Tensor:Fault index-out-of-bounds)'),
        ('(tensor:from-coordinates (2) (tensor:from-values i64 (1) (0)) (tensor:from-values i64 (1) (1)))', '(Tensor:Fault unsupported-dtype)'),
        ('(tensor:from-coordinates (2) (tensor:from-coo index (2) ((0 0))) (tensor:from-values i64 (2) (1 2)))', '(Tensor:Fault shape-mismatch)'),
        (f'(tensor:inspect (tensor:reshape (tensor:view {dense} (1 3) (3 1) 3) (3)))', '(TensorData f64 (3) ((0 4.0) (1 5.0) (2 6.0)))'),
        (f'(tensor:inspect (tensor:reshape (tensor:view {dense} (3 2) (1 3) 0) (6)))', '(TensorData f64 (6) ((0 1.0) (1 4.0) (2 2.0) (3 5.0) (4 3.0) (5 6.0)))'),
        (f'(tensor:reshape {dense} (7))', '(Tensor:Fault shape-mismatch)'),
    ]
    checks = 0
    for language, profile in [('petta', None), ('petta', 'extended'),
                              ('he', 'extended'), ('prime', None)]:
        library = '(library tensor)' if language.startswith('petta') else 'tensor'
        prefix = f'!(import! &self {library})\n(= (tensor-test:square $x) (* $x $x))\n(= (tensor-test:subtract $x $y) (- $x $y))\n(= (tensor:sum Custom) 23)\n'
        source = prefix + ''.join(f'!(let $actual {body} (== $actual {expected}))\n' for body, expected in cases)
        if language.startswith('petta'):
            source = source.replace('True', 'true').replace('False', 'false')
        command = [str(args.cetta.resolve()), '--lang', language]
        if profile:
            command += ['--profile', profile]
        result = subprocess.run(command + ['-e', source], cwd=ROOT,
                                capture_output=True, text=True, timeout=180)
        truth = 'true' if language.startswith('petta') else '[True]'
        actual = result.stdout.splitlines()[-len(cases):]
        if result.returncode or actual != [truth]*len(cases):
            detail = [(i, row, cases[i]) for i, row in enumerate(actual) if row != truth]
            raise AssertionError((language, profile, detail, result.stdout[-3000:], result.stderr[-2000:]))
        checks += len(cases)
        # Arbitrary callbacks retain choice and effects on the authored route.
        choice=prefix+'''\n
(= (tensor-test:choose $a $b) (+ $a $b))
(= (tensor-test:choose $a $b) (- $a $b))
!(collapse (tensor:at (tensor:from-coo i64 (1) ((0 4) (0 2))
                        (combine tensor-test:choose)) 0))
'''
        run=subprocess.run(command+['-e',choice],cwd=ROOT,capture_output=True,text=True,timeout=180)
        expected='((tensor:present 6) (tensor:present 2))'
        if not language.startswith('petta'):
            expected='['+expected+']'
        assert run.returncode==0 and run.stdout.splitlines()[-1:]==[expected],(language,run.stdout,run.stderr)
        checks+=1
        effect=prefix+'''\n
(= (tensor-test:effect $a $b) (let $printed (println! "TensorCombineEffect") (+ $a $b)))
!(tensor:at (tensor:from-coo i64 (1) ((0 1) (0 2) (0 3)) (combine tensor-test:effect)) 0)
'''
        run=subprocess.run(command+['-e',effect],cwd=ROOT,capture_output=True,text=True,timeout=180)
        expected='(tensor:present 6)' if language.startswith('petta') else '[(tensor:present 6)]'
        assert run.returncode==0 and run.stdout.count('TensorCombineEffect')==2 and run.stdout.splitlines()[-1:]==[expected],(language,run.stdout,run.stderr)
        checks+=1
    print(f'TensorWorkflows {checks} 0')


if __name__ == '__main__':
    main()
