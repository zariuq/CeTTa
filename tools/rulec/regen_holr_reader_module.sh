#!/bin/sh
# Regenerate native/holr_reader_module/generated/ from the HOLR stream reader's modules.
#
#   sh tools/rulec/regen_holr_reader_module.sh READERS-DIR [CETTA-BINARY [MODULE-TOOLS-DIR]]
#   sh tools/rulec/regen_holr_reader_module.sh --wire WIRE-FILE
#
# READERS-DIR holds the reader modules, bytes/ and hol/ (kept in Mettapedia as
# metta/gslt/module/readers); MODULE-TOOLS-DIR (by default READERS-DIR/../tools) holds the
# module-authoring client's source (module_authoring.c, authoring.mk).  From the CeTTa root, for
# example:
#
#   sh tools/rulec/regen_holr_reader_module.sh <Mettapedia checkout>/metta/gslt/module/readers
#
# CeTTa assembles the reader's theory (HOLRStream, theory Format) as the modules' own library does
# (bytes/lib_byte_modules.metta) and writes the compile request; the module-authoring client exports
# the core and its ordered rules; tools/rulec/rules_to_wire.metta prints the core as rows
# (generated/HOLRStream.wire, kept so that --wire regenerates from it alone).  rulec
# (tools/rulec/compiler) compiles the rows, every judgment a function (--functional), with
# native/holr_reader_module/holr_reader.bindings;
# rulec_validate checks every DAG against the rules and the generated dispatch code against the DAGs.
# Any failure stops before generated/ changes.  Then the instance's names (tools/rulec/instance_names.sh):
# every global name of hrm_instance.c but cetta_holr_reader gets the prefix hrm_
# (generated/holr_reader_instance_names.h).  Everything is made in a staging copy.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$ROOT/runtime/rulec-holr-reader
STAGE=$WORK/stage
GEN=$ROOT/native/holr_reader_module/generated
SG=$STAGE/native/holr_reader_module/generated
rm -rf "$STAGE"
mkdir -p "$WORK" "$GEN" "$SG"
CC=${CC:-cc}
if [ "$1" = "--wire" ]; then
  cp "$2" "$WORK/HOLRStream.wire"
  DIGEST=$(sha256sum "$WORK/HOLRStream.wire" | cut -d' ' -f1)   # the rows' digest (no module files given)
else
  RD=$(cd "$1" && pwd)
  CETTA=${2:-$ROOT/cetta}
  MODDIR=$(cd "${3:-$RD/../tools}" && pwd)
  # the module-authoring client, built against this tree
  (cd "$ROOT" && make --no-print-directory -f Makefile -f "$MODDIR/authoring.mk" module-authoring-client \
     MODULE_TOOLS_SOURCE_DIR="$MODDIR" MODULE_TOOLS_OUTPUT_DIR="$WORK" > "$WORK/authoring.log" 2>&1)
  cat > "$WORK/request.metta" <<EOM
!(import! &self "$RD/bytes/lib_byte_modules.metta")
!(import! &self fs)
!(let* (((ByteModuleContext \$decode \$arithmetic \$records \$root \$d \$a \$r \$h \$catalog)
          (byte-module:holr-context "$RD/bytes" "$RD/hol"))
        (\$request (ModuleCompilePipeline \$root (LCons "Format" LNil) (LCons \$r LNil) \$catalog "HOLRStream")))
   (fs:write "$WORK/request.txt" (repr \$request)))
EOM
  rm -f "$WORK/request.txt"
  "$CETTA" "$WORK/request.metta" > /dev/null
  "$WORK/module-authoring" 400000000 "$WORK/request.txt" "$WORK/rules.txt" 2> /dev/null
  "$CETTA" --lang he "$ROOT/tools/rulec/rules_to_wire.metta" "$WORK/rules.txt" | grep '^(' > "$WORK/HOLRStream.wire"
  # the modules' digest: SHA-256 of the sorted list of their files' SHA-256 sums
  DIGEST=$(cd "$RD" && sha256sum bytes/*.module hol/*.module | sort -k2 | sha256sum | cut -d' ' -f1)
fi
tail -1 "$WORK/HOLRStream.wire" | grep -q '^(wire-end ' || { echo "regen: no wire rows" >&2; exit 1; }
$CC -O2 -o "$WORK/rulec" "$ROOT"/tools/rulec/compiler/*.c
$CC -O2 -o "$WORK/rulec_validate" "$ROOT/tools/rulec/rulec_validate.c"
"$WORK/rulec" "$WORK/HOLRStream.wire" --functional --cache-constants \
  --census "$SG/census.txt" --census-tsv "$WORK/census.tsv" --dag "$SG/HOLRStream.dag" \
  --bindings "$ROOT/native/holr_reader_module/holr_reader.bindings" \
  --include-rt ../../rule_runtime/rc_rt.h --component-prefix ../../rule_components/ --source-digest "$DIGEST" \
  --emit "$SG/holr_reader.c" --emit-h "$SG/holr_reader_ids.h"
"$WORK/rulec_validate" "$WORK/HOLRStream.wire" "$SG/HOLRStream.dag" "$SG/holr_reader.c"
cp -r "$ROOT/native/rule_runtime" "$ROOT/native/rule_components" "$ROOT/native/rule_module" "$STAGE/native/"
cp "$ROOT/native/holr_reader_module/hrm_instance.c" "$STAGE/native/holr_reader_module/"
sh "$ROOT/tools/rulec/instance_names.sh" "$STAGE/native/holr_reader_module/hrm_instance.c" "$SG/holr_reader_instance_names.h" hrm_ cetta_holr_reader \
  -std=c11 -pthread -I"$ROOT/src" -I"$ROOT"
cp "$WORK/HOLRStream.wire" "$SG/"
cp "$SG/holr_reader.c" "$SG/holr_reader_ids.h" "$SG/holr_reader_instance_names.h" "$SG/HOLRStream.dag" "$SG/census.txt" "$SG/HOLRStream.wire" "$GEN/"
rm -rf "$STAGE"
echo "regenerated $GEN from the reader modules' digest $DIGEST"
