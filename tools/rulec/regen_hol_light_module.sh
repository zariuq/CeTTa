#!/bin/sh
# Regenerate native/hol_light_module/generated/ from the HOL Light kernel module.
#
#   sh tools/rulec/regen_hol_light_module.sh MODULE-FILE [CETTA-BINARY]
#
# MODULE-FILE is the HOL Light kernel module, kept in Mettapedia as
# metta/gslt/module/HOLLightKernel.module; for example, from the CeTTa root:
#
#   sh tools/rulec/regen_hol_light_module.sh <Mettapedia checkout>/metta/gslt/module/HOLLightKernel.module
#
# CeTTa's own module reader prints the module's core rows (tools/rulec/dump_wire.metta); rulec
# compiles them (census, decision DAGs, C); rulec_validate checks every DAG against the rules and the
# generated dispatch code against the DAGs.  Any failure stops before generated/ changes.
# Representations are fixed below: names, types and terms as the native kernel's interned objects
# (native/hol_light_module/holr_views.h), id tables as arrays (holr_tables.c).  The justified blocks
# and reflexivity laws, each with its label and justification, are the rows of
# native/hol_light_module/hol_blocks.table (bound in hol_blocks.c, holr_tables.c, hol_native_ops.c).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MODULE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
CETTA=${2:-$ROOT/cetta}
WORK=$ROOT/runtime/rulec
GEN=$ROOT/native/hol_light_module/generated
mkdir -p "$WORK" "$GEN"
CC=${CC:-cc}
$CC -O2 -o "$WORK/rulec" "$ROOT/tools/rulec/rulec.c"
$CC -O2 -o "$WORK/rulec_validate" "$ROOT/tools/rulec/rulec_validate.c"
$CC -O2 -o "$WORK/rulec_tracecheck" "$ROOT/tools/rulec/rulec_tracecheck.c"
printf '!(import! &self "%s")\n!(wire:dump "%s" "Kernel")\n' "$ROOT/tools/rulec/dump_wire.metta" "$MODULE" > "$WORK/dump.metta"
"$CETTA" "$WORK/dump.metta" | grep '^(' > "$WORK/HOLLightKernel.wire"
SHA=$(sha256sum "$MODULE" | cut -d' ' -f1)
"$WORK/rulec" "$WORK/HOLLightKernel.wire" \
  --layers data=not-yes,syntax=bool-ty,kernel=type-of-var,rules=REFL,theory=initial-theory,replay=policy-max-open \
  --census "$WORK/census.txt" --census-tsv "$WORK/census.tsv" --dag "$WORK/HOLLightKernel.dag" \
  --safepoint hl:run --native-sort 'Records=records(hl:rnil,hl:rcons)' --cache-constants \
  --native-sort 'Nat=nat(hl:nz,hl:nb0,hl:nb1)' \
  --native-sort 'Str=ext:holname(hl:nnil,hl:ncons)' \
  --native-sort 'Type=ext:holtype(hl:Tyvar,hl:Tyapp)' \
  --native-sort 'Term=ext:holterm(hl:Var,hl:Const,hl:Comb,hl:Abs)' \
  --native-sort 'Table=ext:holtable(hl:table)' \
  --rep-header ../holr_views.h --blocks "$ROOT/native/hol_light_module/hol_blocks.table" \
  --include-rt ../../rule_runtime/rc_rt.h --source-digest "$SHA" \
  --emit "$WORK/hol_module.c" --emit-h "$WORK/hol_module_ids.h"
"$WORK/rulec_validate" "$WORK/HOLLightKernel.wire" "$WORK/HOLLightKernel.dag" "$WORK/hol_module.c"
cp "$WORK/hol_module.c" "$WORK/hol_module_ids.h" "$WORK/HOLLightKernel.dag" "$WORK/census.txt" "$GEN/"
echo "regenerated $GEN from module SHA-256 $SHA"
