#!/usr/bin/env python3
"""Lower the collection key-encoding operation and result field contract."""
import argparse
import hashlib
from pathlib import Path

from generate_postings_descriptors import read_descriptor_module, descriptor_pattern
from generate_tensor_descriptors import record, nullary
from gslt2parse_schema_v1 import Symbol
from library_spelling import spelling

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'langdef/collections/KeyEncoding.module'


def generated(binary):
    terms, equations = read_descriptor_module(binary,
        'tests/langdef/collections/key_encoding_runtime_v1.metta', 'CollectionKeyEncoding')
    rows = {}
    for declaration in equations:
        _, equation = record(declaration, 'ModuleDeclarationV1', 2)
        _, context, left, right = record(equation, 'ModuleEquationV1', 4)
        kind, arguments = descriptor_pattern(left)
        constructor, fields = descriptor_pattern(right)
        if (context != Symbol('LNil') or len(arguments) != 1 or
                nullary(arguments[0]) != 'Factorize' or kind in rows):
            raise ValueError('expected closed unique Factorize clauses')
        rows[kind] = (constructor, [nullary(field) for field in fields])
    if set(rows) != {'CollectionOperation', 'CollectionResult', 'CollectionObservation'}:
        raise ValueError('operation, result and observation clauses required')
    operation, operands = rows['CollectionOperation']
    result, fields = rows['CollectionResult']
    observe, policies = rows['CollectionObservation']
    if (operation != 'Operation' or operands != ['WireFactorize', 'Keys'] or
            result != 'Result' or fields != ['WireResult', 'Keys', 'Codes', 'FirstPositions'] or
            observe != 'Observe' or len(policies) != len(set(policies))):
        raise ValueError('unsupported key-encoding schema')
    wire, result_head = terms[operands[0]], terms[fields[0]]
    if wire != '__cetta_lib_list_factorize' or result_head != 'List:Factorization':
        raise ValueError('unsupported wire spelling')
    digest = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    header = ['/* Generated from CollectionKeyEncoding; do not edit. */',
        '#ifndef CETTA_COLLECTION_KEYS_GENERATED_H', '#define CETTA_COLLECTION_KEYS_GENERATED_H',
        f'#define CETTA_COLLECTION_KEYS_SHA256 "{digest}"',
        f'#define CETTA_COLLECTION_KEYS_HEAD "{wire}"',
        f'#define CETTA_COLLECTION_KEYS_RESULT "{result_head}"',
        f'#define CETTA_COLLECTION_KEYS_RESULT_WIDTH {len(fields)}u']
    header += [f'#define CETTA_COLLECTION_KEYS_{field.upper()} {index}u'
               for index, field in enumerate(fields[1:], 1)]
    header += [f'#define CETTA_COLLECTION_KEYS_POLICY "{"|".join(policies)}"', '#endif', '']
    metta = ('; Generated from CollectionKeyEncoding; do not edit.\n'
        '(= (collection:key-encoding-descriptor)\n'
        f'   (CollectionKeyEncoding {wire} ({" ".join(operands[1:])})\n'
        f'     ({result_head} {" ".join(fields[1:])}) ({" ".join(policies)})))\n')
    lean_rows = ',\n  '.join('("' + kind + '", "' + constructor + '", [' +
        ', '.join('"' + field + '"' for field in row) + '])'
        for kind, (constructor, row) in rows.items())
    lean = f'''import Mettapedia.GSLT.LanguageDef.ModuleSource.Commands
open Mettapedia.GSLT.LanguageDef.ModuleSource.Commands
set_option maxRecDepth 4096
set_option maxHeartbeats 800000
#module_file "../KeyEncoding.module"
namespace CeTTaCollectionKeyEncoding
open Mettapedia.OSLF.MeTTaIL.Syntax
private def language : LanguageDef :=
  CollectionKeyEncoding.language.toOption.getD (LanguageDef.empty "invalid")
theorem source_loaded : CollectionKeyEncoding.language = .ok language := rfl
def rows : List (String × String × List String) := [
  {lean_rows}]
def sourceRows : List (String × Pattern) :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply label [Pattern.apply "Factorize" []] => some (label, equation.right)
    | _ => none
theorem rows_match_source : sourceRows = rows.map
    (fun (label, constructor, fields) =>
      (label, Pattern.apply constructor (fields.map fun field => Pattern.apply field []))) := by
  decide +kernel
#print axioms rows_match_source
end CeTTaCollectionKeyEncoding
'''
    return {ROOT / 'src/generated/collection_keys.generated.h': '\n'.join(header),
            ROOT / 'lib/collection_keys.metta': metta,
            ROOT / 'lib/petta/collection_keys.metta': spelling(metta, 'petta'),
            ROOT / 'langdef/collections/generated/KeyEncoding.lean': lean}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for path, content in generated(args.cetta.resolve()).items():
        if args.check:
            if not path.is_file() or path.read_text() != content:
                raise SystemExit(f'out of date: {path.relative_to(ROOT)}')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    print('CollectionKeyDescriptors 4 0')


if __name__ == '__main__':
    main()
