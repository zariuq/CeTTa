#!/usr/bin/env bash
# Planted-mutation harness for ground-call memoization: each build injects one
# defect; the paired golden MUST fail (proving the golden is load-bearing).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
declare -A CATCH=(
  [GROUND_MEMO_MUTATION_BAG_TO_SET]=tests/test_ground_call_multiplicity.metta
  [GROUND_MEMO_MUTATION_IGNORE_EPOCH]=tests/test_ground_call_invalidation.metta
  [GROUND_MEMO_MUTATION_ADMIT_IMPURE]=tests/test_ground_call_state_not_cached.metta
)
fail=0
for mut in "${!CATCH[@]}"; do
  bin="cetta-mut-$(echo "$mut" | tr 'A-Z_' 'a-z-')"
  nice -n 19 make -B BUILD=core PRIME_RECEIPT_INDEX_CPPFLAGS="-D$mut" \
       BIN="$bin" -j"$(nproc)" >/dev/null 2>&1
  out="$(timeout 60 "./$bin" --lang prime "${CATCH[$mut]}" 2>&1)"
  if [ "$out" = "[()]" ]; then
    echo "NOT CAUGHT: $mut — ${CATCH[$mut]} still passed (golden is not load-bearing)"
    fail=$((fail+1))
  else
    echo "CAUGHT: $mut — $(basename "${CATCH[$mut]}") failed as required"
  fi
  rm -f "./$bin"
done
[ "$fail" -eq 0 ] && echo "ALL MUTATIONS CAUGHT" || echo "$fail MUTATION(S) NOT CAUGHT"
exit "$fail"
