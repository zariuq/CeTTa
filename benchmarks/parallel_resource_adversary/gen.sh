#!/usr/bin/env bash
# Parallel resource adversary: N allocation-heavy branches under once(hyperpose),
# one cheap winner at an authored position.  Probes three laws at once:
#   memory conservation  - worker RSS must not scale as workers x full-nursery
#   cancellation quality - wall must track the winner, not the losers
#   order robustness     - winner position must not starve (source-order trap)
# Usage: gen.sh BRANCHES WINNER_POS SMALL HUGE > out.metta   (1-indexed winner)
set -euo pipefail
N="${1:?branches}"; W="${2:?winner-pos}"; SMALL="${3:-2000}"; HUGE="${4:-500000}"
cat <<'P'
(= (build $k $acc) (if (== $k 0) $acc (build (- $k 1) (cons-atom $k $acc))))
(= (churn $n) (if (== $n 0) found (let $junk (build 64 ()) (churn (- $n 1)))))
P
printf '!(once (hyperpose ('
for ((i=1;i<=N;i++)); do
  if [ "$i" -eq "$W" ]; then printf '(churn %s) ' "$SMALL"; else printf '(churn %s) ' "$HUGE"; fi
done
printf ')))\n'
