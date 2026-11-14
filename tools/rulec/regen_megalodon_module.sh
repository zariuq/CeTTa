#!/bin/sh
# Regenerate native/megalodon_module/generated/ from the Megalodon kernel module.
#
#   sh tools/rulec/regen_megalodon_module.sh MODULE-FILE [CETTA-BINARY]
#
# MODULE-FILE is the Megalodon kernel module, kept in Mettapedia as
# metta/gslt/module/MegalodonKernel.module.  As for the HOL Light module (regen_hol_light_module.sh):
# CeTTa's own module reader prints the module's core rows, rulec compiles them, rulec_validate checks
# every DAG against the rules (with the leaves where overlapping rules are told apart by their
# premises) and the generated dispatch code against the DAGs.  The representations and components
# are the rows of native/megalodon_module/mgk.bindings.  Then the instance's names
# (tools/rulec/instance_names.sh): every global name of mgm_instance.c but cetta_mgm_dispatch gets the
# prefix mgk_ (generated/mgk_instance_names.h).  Everything is made in a staging copy; any failure
# stops before generated/ changes.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MODULE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
CETTA=${2:-$ROOT/cetta}
WORK=$ROOT/runtime/rulec-megalodon
STAGE=$WORK/stage
GEN=$ROOT/native/megalodon_module/generated
SG=$STAGE/native/megalodon_module/generated
CC=${CC:-cc}
rm -rf "$STAGE"
mkdir -p "$WORK" "$SG"
$CC -O2 -o "$WORK/rulec" "$ROOT"/tools/rulec/compiler/*.c
$CC -O2 -o "$WORK/rulec_validate" "$ROOT/tools/rulec/rulec_validate.c"
printf '!(import! &self "%s")\n!(wire:dump "%s" "Kernel")\n' "$ROOT/tools/rulec/dump_wire.metta" "$MODULE" > "$WORK/dump.metta"
"$CETTA" "$WORK/dump.metta" | grep '^(' > "$WORK/MegalodonKernel.wire"
SHA=$(sha256sum "$MODULE" | cut -d' ' -f1)
"$WORK/rulec" "$WORK/MegalodonKernel.wire" \
  --census "$SG/census.txt" --census-tsv "$WORK/census.tsv" --dag "$SG/MegalodonKernel.dag" \
  --bindings "$ROOT/native/megalodon_module/mgk.bindings" \
  --include-rt ../../rule_runtime/rc_rt.h --component-prefix ../../rule_components/ --source-digest "$SHA" \
  --emit "$SG/mgk_module.c" --emit-h "$SG/mgk_module_ids.h"
"$WORK/rulec_validate" "$WORK/MegalodonKernel.wire" "$SG/MegalodonKernel.dag" "$SG/mgk_module.c"
# the instance's names
cp -r "$ROOT/native/rule_runtime" "$ROOT/native/rule_components" "$ROOT/native/rule_module" "$STAGE/native/"
cp "$ROOT/native/megalodon_module/mgm_instance.c" "$STAGE/native/megalodon_module/"
sh "$ROOT/tools/rulec/instance_names.sh" "$STAGE/native/megalodon_module/mgm_instance.c" "$SG/mgk_instance_names.h" mgk_ cetta_mgm_dispatch \
  -std=c11 -pthread -I"$ROOT/src" -I"$ROOT"
cp "$SG/mgk_module.c" "$SG/mgk_module_ids.h" "$SG/mgk_instance_names.h" "$SG/MegalodonKernel.dag" "$SG/census.txt" "$GEN/"
rm -rf "$STAGE"
echo "regenerated $GEN from module SHA-256 $SHA"
