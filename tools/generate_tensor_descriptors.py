#!/usr/bin/env python3
"""Lower the shared module reader's tensor descriptors into finite tables.

The module grammar and parser are existing infrastructure. This translator
accepts a closed table fragment, not arbitrary native implementation code.
Ordinary runtime builds use the checked-in tables.
"""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess

from gslt2parse_schema_v1 import Symbol, StringLiteral, parse_sexprs
from library_spelling import spelling

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'src/generated/tensor_descriptors.generated.h'
LEAN = ROOT / 'langdef/tensor/generated/Descriptors.lean'
METTA = ROOT / 'lib/tensor_descriptors.metta'
PETTA = ROOT / 'lib/petta/tensor_descriptors.metta'


def record(value, head, arity):
    if not isinstance(value, tuple) or len(value) != arity + 1 or value[0] != Symbol(head):
        raise ValueError(f'expected {head}/{arity}')
    return value[1:]


def linked(value):
    result = []
    while value != Symbol('LNil'):
        item, value = record(value, 'LCons', 2)
        result.append(item)
    return result


def pattern(value):
    name, arguments = record(value, 'PApp', 2)
    if not isinstance(name, StringLiteral) or not re.fullmatch(r'[A-Za-z][A-Za-z0-9]*', name.text):
        raise ValueError('descriptor names must be identifiers')
    return name.text, [pattern(arg) for arg in linked(arguments)]


def nullary(value):
    name, args = value
    if args:
        raise ValueError('expected a nullary descriptor')
    return name


def natural(value, prefix):
    name = nullary(value)
    if not re.fullmatch(prefix + r'[1-9][0-9]*', name):
        raise ValueError(f'expected {prefix}N descriptor')
    return int(name[len(prefix):])


def tables(binary):
    run = subprocess.run([str(binary), '--no-results', '--lang', 'petta@1.0.6', '--profile', 'extended',
        'tests/langdef/tensor/descriptors_runtime_v1.metta'], cwd=ROOT, text=True, capture_output=True, check=True)
    values = parse_sexprs(run.stdout)
    if len(values) != 1:
        raise ValueError('module reader did not return one document')
    _, name, imports, theories, _, _, _ = record(values[0], 'ModuleValueV1', 7)
    if name != StringLiteral('TensorDescriptors') or imports != Symbol('LNil') or len(linked(theories)) != 1:
        raise ValueError('unexpected descriptor module structure')
    _, parameters, body = record(linked(theories)[0], 'ModuleTheoryV1', 3)
    base, equations = record(body, 'ModuleAddEquationsV1', 2)
    while isinstance(base, tuple) and base[0] in (Symbol('ModuleAddTypesV1'), Symbol('ModuleAddTermsV1')):
        base, _ = base[1:]
    if parameters != Symbol('LNil') or base != Symbol('ModuleEmptyV1'):
        raise ValueError('descriptor module must be a closed table')
    scalars, layouts, policies, callback_inputs = [], [], [], []
    seen = set()
    for declaration in linked(equations):
        _, equation = record(declaration, 'ModuleDeclarationV1', 2)
        _, context, left, right = record(equation, 'ModuleEquationV1', 4)
        if context != Symbol('LNil'):
            raise ValueError('descriptor equations must be closed')
        kind, key_args = pattern(left)
        if len(key_args) != 1:
            raise ValueError('expected one descriptor key')
        key = nullary(key_args[0])
        if (kind, key) in seen:
            raise ValueError('duplicate descriptor')
        seen.add((kind, key))
        target, args = pattern(right)
        if kind == 'TensorDType' and target == 'ScalarLayout' and len(args) == 4:
            scalars.append((key, natural(args[0], 'Code'), natural(args[1], 'Bytes'),
                            nullary(args[2]), natural(args[3], 'Precision')))
        elif kind == 'TensorLayout' and target == 'LayoutPolicy' and len(args) == 2:
            layouts.append((key, *(nullary(arg) for arg in args)))
        elif kind == 'TensorCallbackInput' and target == 'ScalarAtomInput' and len(args) == 1:
            callback_inputs.append((key, nullary(args[0])))
        elif kind == 'TensorObservation' and target == 'Observe':
            policies.append((kind, key, *(nullary(arg) for arg in args)))
        elif kind == 'TensorTransport' and target == 'DenseOnly':
            policies.append((kind, key, *(str(natural(arg, 'Code')) for arg in args)))
        else:
            raise ValueError('unsupported descriptor equation')
    if not scalars or len({s[1] for s in scalars}) != len(scalars):
        raise ValueError('dtype codes must be nonempty and unique')
    if any(code > 255 or width not in (1, 4, 8) or precision > width*8
           for _, code, width, _, precision in scalars):
        raise ValueError('unsupported scalar descriptor')
    validate_callback_inputs(scalars, callback_inputs)
    return scalars, layouts, policies, callback_inputs


def validate_callback_inputs(scalars, inputs):
    representations = {
        'i64': ('Signed64Atom', 8, 'Signed', 64),
        'f64': ('Float64Atom', 8, 'Floating', 53),
        'u8': ('Unsigned8Atom', 1, 'Unsigned', 8),
        'f32': ('Float32Atom', 4, 'Floating', 24),
        'index': ('Unsigned64Atom', 8, 'Unsigned', 64),
    }
    layouts = {name: (width, family, precision) for name, _, width, family, precision in scalars}
    if not inputs or any(name not in layouts or name not in representations or
                         (encoding, *layouts[name]) != representations[name]
                         for name, encoding in inputs):
        raise ValueError('unsupported callback scalar encoding')


def generated(binary):
    scalars, layouts, policies, callback_inputs = tables(binary)
    digest = hashlib.sha256((ROOT / 'langdef/tensor/Descriptors.module').read_bytes()).hexdigest()
    header = ['/* Generated from the shared .module reader; do not edit. */',
              '#ifndef CETTA_TENSOR_DESCRIPTORS_GENERATED_H', '#define CETTA_TENSOR_DESCRIPTORS_GENERATED_H',
              f'#define CETTA_TENSOR_DESCRIPTORS_SHA256 "{digest}"', '#define CETTA_TENSOR_DTYPE_ROWS(X) \\']
    for i, (name, code, width, family, precision) in enumerate(scalars):
        header.append(f'    X({name.upper()}, {code}, "{name}", {width}, {family}, {precision})' + (' \\' if i+1 < len(scalars) else ''))
    header.append('typedef enum {')
    for _, encoding in callback_inputs:
        header.append(f'    CETTA_TENSOR_CALLBACK_{encoding.upper()},')
    header.append('} CettaTensorCallbackEncoding;')
    header.append('#define CETTA_TENSOR_CALLBACK_INPUT_ROWS(X) \\')
    for i, (name, encoding) in enumerate(callback_inputs):
        header.append(f'    X({name.upper()}, CETTA_TENSOR_CALLBACK_{encoding.upper()})' +
                      (' \\' if i+1 < len(callback_inputs) else ''))
    header.append('#define CETTA_TENSOR_LAYOUT_ROWS(X) \\')
    for i, (name, support, order) in enumerate(layouts):
        header.append(f'    X({name.upper()}, {i}, "{name}", {support}, {order})' + (' \\' if i+1 < len(layouts) else ''))
    observations = [(key, values) for kind, key, *values in policies if kind == 'TensorObservation']
    header.append('#define CETTA_TENSOR_OBSERVATION_ROWS(X) \\')
    for i, (key, values) in enumerate(observations):
        header.append(f'    X("{key}", "{"|".join(values)}")' + (' \\' if i+1 < len(observations) else ''))
    def exact_promotion(source,target):
        sf,sp=source[3:]
        tf,tp=target[3:]
        if source==target or sf=='Boolean':
            return True
        if sf=='Floating':
            return tf=='Floating' and sp<=tp
        if tf=='Floating':
            return sp-(sf=='Signed')<=tp
        if tf=='Boolean':
            return False
        if sf==tf:
            return sp<=tp
        return sf=='Unsigned' and tf=='Signed' and sp<tp
    promotions=[(source[0],target[0]) for source in scalars for target in scalars
                if exact_promotion(source,target)]
    header.append('#define CETTA_TENSOR_EXACT_PROMOTION_ROWS(X) \\')
    for i,(source,target) in enumerate(promotions):
        header.append(f'    X({source.upper()}, {target.upper()})'+(' \\' if i+1<len(promotions) else ''))
    for _, key, *values in policies:
        if key == 'CTN1':
            header.append('#define CETTA_TENSOR_CTN1_DTYPE_ROWS(X) \\')
            for i, code in enumerate(values):
                name = next(name for name, number, *_ in scalars if str(number) == code)
                header.append(f'    X({name.upper()})' + (' \\' if i+1 < len(values) else ''))
    header.extend(['#endif', ''])
    lean = ['import Mettapedia.GSLT.LanguageDef.ModuleSource.Commands',
            'import Mathlib.Data.List.Pairwise', '',
            'open Mettapedia.GSLT.LanguageDef.ModuleSource.Commands', '',
            'set_option maxRecDepth 4096', 'set_option maxHeartbeats 800000', '',
            '#module_file "../Descriptors.module"', '', 'namespace CeTTaTensorDescriptors',
            'open Mettapedia.OSLF.MeTTaIL.Syntax', '',
            'inductive DType where', '  | ' + ' | '.join(s[0] for s in scalars),
            '  deriving DecidableEq, Repr', '', 'def code : DType → Nat']
    lean += [f'  | .{name} => {code}' for name, code, *_ in scalars]
    lean += ['', 'def byteWidth : DType → Nat']
    lean += [f'  | .{name} => {width}' for name, _, width, *_ in scalars]
    lean += ['', 'theorem byteWidth_positive (dtype : DType) : 0 < byteWidth dtype := by',
             '  cases dtype <;> decide', '',
             'theorem code_injective : Function.Injective code := by',
             '  intro a b h; cases a <;> cases b <;> simp_all [code]', '',
             'def exactPromotion : DType → DType → Bool']
    lean += [f'  | .{source}, .{target} => true' for source,target in promotions]
    lean += ['  | _, _ => false', '',
             'theorem exactPromotion_reflexive (dtype : DType) : exactPromotion dtype dtype = true := by',
             '  cases dtype <;> rfl', '',
             'def scalarRows : List (String × Nat × Nat × String × Nat) := [']
    lean += [f'  ("{name}", {code}, {width}, "{family}", {precision})' + (',' if i+1 < len(scalars) else '')
             for i, (name, code, width, family, precision) in enumerate(scalars)]
    lean += [']', '',
        'private def language : LanguageDef :=',
        '  TensorDescriptors.language.toOption.getD (LanguageDef.empty "invalid")', '',
        'theorem source_loaded : TensorDescriptors.language = .ok language := rfl', '',
        'def sourceScalarRows : List (String × Pattern) :=',
        '  language.equations.filterMap fun equation =>',
        '    match equation.left with',
        '    | Pattern.apply "TensorDType" [Pattern.apply label []] => some (label, equation.right)',
        '    | _ => none', '',
        'def encodedScalarRows : List (String × Pattern) :=',
        '  scalarRows.map fun (label, wireCode, width, kind, mantissa) =>',
        '    (label, Pattern.apply "ScalarLayout" [',
        '      Pattern.apply ("Code" ++ toString wireCode) [], Pattern.apply ("Bytes" ++ toString width) [],',
        '      Pattern.apply kind [], Pattern.apply ("Precision" ++ toString mantissa) []])', '',
        'theorem scalar_rows_match_source : sourceScalarRows = encodedScalarRows := by',
        '  decide +kernel', '',
        '#print axioms scalar_rows_match_source', '',
        'def observationRows : List (String × List String) := [']
    lean += [f'  ("{key}", [{", ".join(chr(34) + value + chr(34) for value in values)}])'
             + (',' if i+1 < len(observations) else '')
             for i, (key, values) in enumerate(observations)]
    lean += [']', '',
        'def sourceObservationRows : List (String × Pattern) :=',
        '  language.equations.filterMap fun equation =>',
        '    match equation.left with',
        '    | Pattern.apply "TensorObservation" [Pattern.apply label []] => some (label, equation.right)',
        '    | _ => none', '',
        'def encodedObservationRows : List (String × Pattern) :=',
        '  observationRows.map fun (label, policies) =>',
        '    (label, Pattern.apply "Observe" (policies.map fun policy => Pattern.apply policy []))', '',
        'theorem observation_rows_match_source : sourceObservationRows = encodedObservationRows := by',
        '  decide +kernel', '',
        '#print axioms observation_rows_match_source', '',
        'def callbackInputRows : List (String × String) := [']
    lean += [f'  ("{name}", "{encoding}")' + (',' if i+1 < len(callback_inputs) else '')
             for i, (name, encoding) in enumerate(callback_inputs)]
    lean += [']', '',
        'def sourceCallbackInputRows : List (String × Pattern) :=',
        '  language.equations.filterMap fun equation =>',
        '    match equation.left with',
        '    | Pattern.apply "TensorCallbackInput" [Pattern.apply label []] => some (label, equation.right)',
        '    | _ => none', '',
        'def encodedCallbackInputRows : List (String × Pattern) :=',
        '  callbackInputRows.map fun (label, encoding) =>',
        '    (label, Pattern.apply "ScalarAtomInput" [Pattern.apply encoding []])', '',
        'theorem callback_input_rows_match_source : sourceCallbackInputRows = encodedCallbackInputRows := by',
        '  decide +kernel', '',
        '#print axioms callback_input_rows_match_source', '',
        'end CeTTaTensorDescriptors', '']
    metta=['; Generated from TensorDescriptors; do not edit.',
           '(: tensor:dtype-descriptor (-> Atom Atom))',
           '(: tensor:layout-descriptor (-> Atom Atom))',
           '(: tensor:observation-descriptor (-> Atom Atom))',
           '(: tensor:transport-descriptor (-> Atom Atom))']
    metta.append('(: tensor:callback-input-descriptor (-> Atom Atom))')
    for name,code,width,family,precision in scalars:
        metta.append(f'(= (tensor:dtype-descriptor {name}) (TensorDType {name} {code} {width} {family} {precision}))')
    for name,support,order in layouts:
        metta.append(f'(= (tensor:layout-descriptor {name}) (TensorLayout {name} {support} {order}))')
    for name, encoding in callback_inputs:
        metta.append(f'(= (tensor:callback-input-descriptor {name}) (TensorCallbackInput {name} {encoding}))')
    for kind,key,*values in policies:
        operation='transport' if kind=='TensorTransport' else 'observation'
        metta.append(f'(= (tensor:{operation}-descriptor {key}) (TensorPolicy {key} ({" ".join(values)})))')
    metta='\n'.join(metta)+'\n'
    return '\n'.join(header), '\n'.join(lean), metta, spelling(metta,'petta')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for path, value in zip((HEADER, LEAN, METTA, PETTA), generated(args.cetta.resolve())):
        if args.check:
            if not path.is_file() or path.read_text() != value:
                raise SystemExit(f'tensor descriptor output differs: {path.relative_to(ROOT)}')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value)
    print('TensorDescriptors 4 0')


if __name__ == '__main__':
    main()
