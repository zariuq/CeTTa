#!/usr/bin/env bash
# Native CST records vs independently executable authored oracle, printed
# byte-for-byte, over the item-2 files. A mutated authored GSLT must disagree.
set -euo pipefail
if (( $# != 4 )); then
  echo 'usage: check_tptp_records_native_equivalence_v1.sh RUNTIME EVIDENCE_DIR SYNTAX_BNF CORPUS_ROOT' >&2
  exit 2
fi
root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
runtime=$(realpath "$1")
evidence="$2"
syntax=$(realpath "$3")
corpus=$(realpath "$4")
if [[ -e "$evidence" ]]; then
  if [[ ! -d "$evidence" || -n $(find "$evidence" -mindepth 1 -print -quit) ]]; then
    echo 'evidence path must be absent or an empty directory' >&2
    exit 2
  fi
else
  mkdir -p "$evidence"
fi
evidence=$(realpath "$evidence")
cd "$root"
admission=langdef/petta/generated/plain_bnf_semantic_admission_v1.metta
families=tests/langdef/tptp/official_syntax_families_v9200.p
authored_src=tests/langdef/tptp/official_syntax_records_differential_authored_v1.metta
native_src=tests/langdef/tptp/official_syntax_records_differential_v1.metta
records_src=langdef/tptp/official_syntax_records_v1.metta
mutated_records="$evidence/official_syntax_records_v1.mutated.metta"
mutated_authored="$evidence/official_syntax_records_differential_authored_v1.mutated.metta"

file_args=(
  "$syntax"
  "$corpus/Problems/PUZ/PUZ001-1.p"
  "$corpus/Problems/PUZ/PUZ001+1.p"
  "$corpus/Problems/SYN/SYN000_1.p"
  "$corpus/Problems/PUZ/PUZ140^2.p"
  "$corpus/Problems/SYN/SYN000_4.p"
  "$families"
)

run() {
  local out="$1"; shift
  if [[ -n "${TPTP_DIFFERENTIAL_TIMEOUT_SECONDS:-}" ]]; then
    /usr/bin/time -f 'wall=%e rss_kb=%M rc=%x' -o "$out.time" \
      timeout --signal=KILL "$TPTP_DIFFERENTIAL_TIMEOUT_SECONDS" \
      "$runtime" --quiet --lang petta --fuel 40000000 \
      "$admission" "$@" > "$out" 2> "$out.err"
  else
    /usr/bin/time -f 'wall=%e rss_kb=%M rc=%x' -o "$out.time" \
      "$runtime" --quiet --lang petta --fuel 40000000 \
      "$admission" "$@" > "$out" 2> "$out.err"
  fi
}

run "$evidence/native.log" "$native_src" "${file_args[@]}"
run "$evidence/authored.log" "$authored_src" "${file_args[@]}"

tags=(empty fof-atom quoted lexical-kinds cnf-or qualified puz001-1 puz001+1 syn000_1 puz140 syn000_4 families)

extract_dump() {
  local input=$1 tag=$2 output=$3
  awk -v tag="$tag" '
    BEGIN { prefix = "(TptpDump " tag " "; count = 0 }
    index($0, prefix) == 1 {
      record = substr($0, length(prefix) + 1)
      if (substr(record, length(record), 1) != ")") exit 3
      print substr(record, 1, length(record) - 1)
      count++
    }
    END { if (count != 1) exit 2 }
  ' "$input" > "$output"
}

for log in "$evidence/native.log" "$evidence/authored.log"; do
  if rg -q '^\(Error ' "$log"; then
    echo "runtime error in $log" >&2
    exit 1
  fi
  if rg -q -F 'TPTP:Unprojected' "$log"; then
    echo "unprojected production in $log" >&2
    exit 1
  fi
  if rg -q -F '(tptp-rec:unknown' "$log"; then
    echo "leftover tptp-rec:unknown in $log" >&2
    exit 1
  fi
done

failed=()
for tag in "${tags[@]}"; do
  native_dump="$evidence/compare.txt.$tag.native"
  authored_dump="$evidence/compare.txt.$tag.authored"
  if ! extract_dump "$evidence/native.log" "$tag" "$native_dump"; then
    echo "native dump missing or malformed: $tag" >&2
    exit 1
  fi
  if ! extract_dump "$evidence/authored.log" "$tag" "$authored_dump"; then
    echo "authored dump missing or malformed: $tag" >&2
    exit 1
  fi
  if ! cmp -s "$native_dump" "$authored_dump"; then
    failed+=("$tag")
  fi
done
if (( ${#failed[@]} != 0 )); then
  printf 'disagree %s\n' "${failed[*]}" > "$evidence/compare.txt"
  echo "native/authored printed records disagreed: ${failed[*]}" >&2
  exit 1
fi
printf 'agree %s\n' "${tags[*]}" > "$evidence/compare.txt"
printf '(TptpNativeAuthoredDifferentialV1'
for tag in "${tags[@]}"; do
  printf ' (%s pass)' "$tag"
done
printf ')\n'

# Mutated-oracle negative control: the authored CNF disjunction fold constructs
# an and-node while the native path remains unchanged.
if ! rg -q -F '(rule disjunction-two ' "$records_src"; then
  echo 'no authored disjunction-two rule to mutate' >&2
  exit 1
fi
awk '
  /\(rule disjunction-two / { in_rule = 1 }
  in_rule && !changed && sub(/\(tptp-rec:or /, "(tptp-rec:and ") {
    changed = 1
    in_rule = 0
  }
  { print }
  END { if (!changed) exit 2 }
' "$records_src" > "$mutated_records"
if cmp -s "$records_src" "$mutated_records"; then
  echo 'mutation did not change the authored source' >&2
  exit 1
fi
sed -e "s|langdef/tptp/official_syntax_records_v1.metta|$mutated_records|" \
  "$authored_src" > "$mutated_authored"
set +e
run "$evidence/mutated.log" "$mutated_authored" "${file_args[@]}"
mut_rc=$?
set -e
if (( mut_rc != 0 )); then
  echo "mutated authored oracle runtime failed" >&2
  exit 1
fi
if ! rg -q '^\(TptpDump ' "$evidence/mutated.log"; then
  echo '(TptpNativeAuthoredDifferentialV1 mutated-rejected)'
  {
    echo "(TptpNativeAuthoredDifferentialV1 verdict agree mutated-rejected)"
    cat "$evidence/compare.txt"
  } | tee "$evidence/summary.txt"
  exit 0
fi
for tag in cnf-or puz001-1; do
  mutated_dump="$evidence/compare.txt.$tag.mutated"
  if ! extract_dump "$evidence/mutated.log" "$tag" "$mutated_dump"; then
    echo "mutated dump missing or malformed: $tag" >&2
    exit 1
  fi
  if cmp -s "$evidence/compare.txt.$tag.native" "$mutated_dump"; then
    echo "mutated authored oracle still agreed on $tag" >&2
    exit 1
  fi
  printf '(TptpNativeAuthored %s fail)\n' "$tag"
done
echo '(TptpNativeAuthoredDifferentialV1 mutated-disagrees)'

{
  echo "(TptpNativeAuthoredDifferentialV1 verdict agree mutated-disagrees)"
  cat "$evidence/compare.txt"
} | tee "$evidence/summary.txt"
