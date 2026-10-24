#!/usr/bin/env python3
"""Lower the authored finite collection-ingress consumer contract."""
import argparse
import hashlib
from pathlib import Path

from generate_postings_descriptors import read_descriptor_module, descriptor_pattern
from generate_tensor_descriptors import record, nullary
from gslt2parse_schema_v1 import Symbol
from library_spelling import spelling

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'langdef/collections/Ingress.module'


def generated(binary):
    terms, equations = read_descriptor_module(binary,
        'tests/langdef/collections/ingress_runtime_v1.metta', 'CollectionIngress')
    rows = {}
    for declaration in equations:
        _, equation = record(declaration, 'ModuleDeclarationV1', 2)
        _, context, left, right = record(equation, 'ModuleEquationV1', 4)
        kind, labels = descriptor_pattern(left)
        target, args = descriptor_pattern(right)
        if context != Symbol('LNil') or len(labels) != 1 or nullary(labels[0]) != 'Append':
            raise ValueError('expected the closed Append consumer')
        if kind in rows:
            raise ValueError('duplicate consumer clause')
        rows[kind] = (target, [nullary(arg) for arg in args])
    if set(rows) != {'CollectionConsumer', 'CollectionObservation'}:
        raise ValueError('expected consumer and observation clauses')
    consumer, fields = rows['CollectionConsumer']
    observation, policies = rows['CollectionObservation']
    if (consumer != 'Consumer' or observation != 'Observe' or len(fields) != 3 or
            fields[1:] != ['Destination', 'Collection'] or len(set(policies)) != len(policies)):
        raise ValueError('invalid consumer schema')
    wire = terms.get(fields[0])
    if wire != 'add-atoms':
        raise ValueError('unsupported native sink')
    digest = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    header = ['/* Generated from CollectionIngress; do not edit. */',
        '#ifndef CETTA_COLLECTION_INGRESS_GENERATED_H', '#define CETTA_COLLECTION_INGRESS_GENERATED_H',
        f'#define CETTA_COLLECTION_INGRESS_SHA256 "{digest}"',
        f'#define CETTA_COLLECTION_INGRESS_HEAD "{wire}"',
        f'#define CETTA_COLLECTION_INGRESS_ARITY {len(fields)-1}u']
    header += [f'#define CETTA_COLLECTION_INGRESS_{name.upper()} {index}u'
               for index, name in enumerate(fields[1:], 1)]
    header += [f'#define CETTA_COLLECTION_INGRESS_POLICY "{"|".join(policies)}"', '#endif', '']
    metta = ('; Generated from CollectionIngress; do not edit.\n'
        '(: collection:ingress-descriptor (-> Atom Atom))\n'
        f'(= (collection:ingress-descriptor append) (CollectionConsumer {wire} '
        f'({" ".join(fields[1:])}) ({" ".join(policies)})))\n')
    field_names = ', '.join(f'"{field}"' for field in fields)
    policy_names = ', '.join(f'"{policy}"' for policy in policies)
    lean = f'''import Mettapedia.GSLT.LanguageDef.ModuleSource.Commands

open Mettapedia.GSLT.LanguageDef.ModuleSource.Commands
set_option maxRecDepth 4096
set_option maxHeartbeats 800000
#module_file "../Ingress.module"

namespace CeTTaCollectionIngress
open Mettapedia.OSLF.MeTTaIL.Syntax
private def language : LanguageDef :=
  CollectionIngress.language.toOption.getD (LanguageDef.empty "invalid")
theorem source_loaded : CollectionIngress.language = .ok language := rfl

def sourceRows (head : String) : List Pattern :=
  language.equations.filterMap fun equation =>
    match equation.left with
    | Pattern.apply label [Pattern.apply "Append" []] =>
        if label = head then some equation.right else none
    | _ => none

def fields : List String := [{field_names}]
def policies : List String := [{policy_names}]
theorem consumer_matches_source :
    sourceRows "CollectionConsumer" =
      [Pattern.apply "Consumer" (fields.map (fun label => Pattern.apply label []))] := by
  decide +kernel
theorem observation_matches_source :
    sourceRows "CollectionObservation" =
      [Pattern.apply "Observe" (policies.map (fun label => Pattern.apply label []))] := by
  decide +kernel
#print axioms consumer_matches_source
#print axioms observation_matches_source
end CeTTaCollectionIngress
'''
    return {
        ROOT / 'src/generated/collection_ingress.generated.h': '\n'.join(header),
        ROOT / 'lib/collection_ingress.metta': metta,
        ROOT / 'lib/petta/collection_ingress.metta': spelling(metta, 'petta'),
        ROOT / 'langdef/collections/generated/Ingress.lean': lean,
    }


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
    print('CollectionIngressDescriptors 4 0')


if __name__ == '__main__':
    main()
