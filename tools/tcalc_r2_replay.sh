#!/usr/bin/env bash
# Replay selected native-kernel calls, preserving the R2 driver's full comparison.
# The manifest has four tab-separated columns: family, name, theory, trace.
# Use "none" for a theory installed by the trace itself. Inputs remain read-only.
set -euo pipefail
if test "$#" != 3; then echo 'usage: tcalc_r2_replay.sh R2_PACKET WORK_DIRECTORY MANIFEST' >&2; exit 2; fi
packet=$(cd "$1" && pwd)
mkdir -p "$2"
work=$(cd "$2" && pwd)
manifest=$(realpath "$3")
repo=$(cd "$(dirname "$0")/.." && pwd)
cetta=${CETTA:-$repo/cetta}
decide=${R2_DECIDE:-$repo/runtime/tcalc-r2-decide}
runtime_root=$(dirname "$(realpath "$cetta")")
if test ! -d "$runtime_root/lib"; then runtime_root=$(dirname "$runtime_root"); fi
if test ! -d "$runtime_root/lib"; then
 echo 'CETTA needs its runtime library assets; keep a frozen binary in the runtime directory' >&2; exit 2
fi
old='(match &kb (occ $o $s $t) ($s $t))'
if test "$(grep -Fc '(match $ls (lres $q $t $s) ($t $s))' "$packet/chainer.metta")" != 1; then
 echo 'proof reader source changed; its named lookup needs review' >&2; exit 2
fi
if test "$(grep -Fc '(install-derived $id $name $nb $space $hyps $concl)' "$packet/chainer.metta")" != 2; then
 echo 'derived-rule assembly source changed; its scope repair needs review' >&2; exit 2
fi
if test "$(grep -Fc "$old" "$packet/chainer.metta")" != 1; then
 echo 'occurrence-reader source changed; its transform lowering needs review' >&2; exit 2
fi
sed '/^(= (install-derived /!s/(install-derived \$id \$name \$nb \$space \$hyps \$concl)/(r2-install-derived $id $name $nb $space $hyps $concl)/;/^(= (lemma-match /!s/(lemma-match \$rule \$inst \$prems \$target \$hyps \$space)/(r2-lemma-match $rule $inst $prems $target $hyps $space)/;s/(match \$ls (lres \$q \$t \$s) (\$t \$s))/(r2-read-lemma $ls $q)/;s/(match \&kb (occ \$o \$s \$t) (\$s \$t))/(r2-read-occurrence $o)/' "$packet/chainer.metta" > "$work/chainer-transform.metta"
sha256sum "$cetta" "$decide" "$packet/chainer.metta" "$packet/driver.metta" \
 "$repo/tools/tcalc_r2_receipts.metta" "$repo/tools/tcalc_r2_scoped_rules.metta" \
 "$runtime_root/lib/petta/str.metta" > "$work/inputs.sha256"
printf 'family\tname\tengine\tprotocol_status\ttcalc_firings\tdirect_firings\n' > "$work/results.tsv"
status=0
while IFS=$'\t' read -r family name theory trace; do
 test -n "$family" || continue
 test "$family" != family || continue
 if grep -Fq 'R2VariableSlot' "$trace" || { test "$theory" != none && grep -Fq 'R2VariableSlot' "$theory"; }; then
  echo 'receipt marker occurs in an input' >&2; exit 2
 fi
 case "$name" in *[!a-zA-Z0-9_.:-]*) echo 'invalid case name' >&2; exit 2;; esac
 printf '' > "$work/load.metta"
 if test "$theory" != none; then printf '!(import! &rb "%s")\n' "$theory" >> "$work/load.metta"; fi
 printf '!(begin-file %s)\n' "$name" >> "$work/load.metta"
 sed 's/^!(t /!(r2-t /' "$trace" > "$work/current.trace.metta"
 sha256sum "$trace" >> "$work/inputs.sha256"
 if test "$theory" != none; then sha256sum "$theory" >> "$work/inputs.sha256"; fi
 for engine in direct tcalc; do
  stem="$work/$family-$name-$engine"
  CETTA_TRANSFORM_ENGINE=$engine CETTA_TRANSFORM_TRACE=1 "$cetta" --lang petta --profile extended --no-results \
   "$work/chainer-transform.metta" "$packet/driver.metta" "$repo/tools/tcalc_r2_scoped_rules.metta" "$repo/tools/tcalc_r2_receipts.metta" \
   "$work/load.metta" "$work/current.trace.metta" > "$stem.out" 2> "$stem.trace"
  set +e
  "$decide" "$stem.out" > "$stem.protocol" 2> "$stem.protocol.err"
  code=$?
  set -e
  grep -E '^\((result|verdict|prefix-result)' "$stem.out" > "$stem.summary" || code=2
  if grep -E '^\((DIFFER|HOLDS-DIFFER|STEP-DIFFER|UNCOMPARED)' "$stem.out" > "$stem.disagreements"; then code=1; fi
  if grep -Eq '\((calls|operations|store)-differ [1-9]' "$stem.summary"; then code=1; fi
  tc=$(grep -c '(engine tcalc)' "$stem.trace" || true)
  direct=$(grep -c '(engine direct)' "$stem.trace" || true)
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$family" "$name" "$engine" "$code" "$tc" "$direct" >> "$work/results.tsv"
  if test "$code" != 0; then status=1; fi
  if test "$engine" = tcalc; then
   if ! cmp -s "$stem.out" <(gzip -dc "$work/$family-$name-direct.out.gz"); then
    printf '(R2EngineDifference %s %s)\n' "$family" "$name" >> "$work/engine-differences.log"; status=1
   fi
  fi
  gzip -f "$stem.out" "$stem.trace"
  printf '%s/%s %s: status %s, tcalc firings %s\n' "$family" "$name" "$engine" "$code" "$tc"
 done
done < "$manifest"
sha256sum --check --quiet "$work/inputs.sha256"
exit "$status"
