#!/usr/bin/env bash
# Row 08 — Roman Treutlein chain+noise problem (the branching-probe spine).
# Faithful to build_chain_problem() in
#   repos/PeTTaChainer/pettachainer/benchmarks/forward_vs_backward.py
# Emits a self-contained HE .metta file: nilbc-style backward chainer (bc)
# + the chain+noise KB at the given depth/branching + the target query.
#
# Chain (the one true spine):  seed:(Reach g0) -> reach_0 -> (Reach g1) -> ... -> (Reach gD)
# Noise (dead-end fan-out):    for each i, for each b:  (Reach gi) -> (Noise n_i_b)
# Backward chaining for (Reach gD) is LINEAR in depth (noise concludes Noise,
# never Reach, so it is never selected backward). The noise only fans out the
# FORWARD lane. This file is the BACKWARD lane: clean per-step depth cost.
#
# Usage: gen_kb.sh DEPTH BRANCHING > out.metta
set -euo pipefail
DEPTH="${1:?depth}"
BRANCH="${2:?branching}"

cat <<'PRELUDE'
;; ---- Nat + minimal backward chainer (nilbc-style, unary rules) -------------
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(: bc (-> $a Nat $b $b))
(= (bc $kb $_ (: $proof $theorem))
   (match $kb (: $proof $theorem) (: $proof $theorem)))
(= (bc $kb (S $depth) (: ($rule $prem) $theorem))
   (let* (((: $rule (-> (: $prem $ptype) $theorem))
            (bc $kb $depth (: $rule (-> (: $prem $ptype) $theorem))))
          ((: $prem $ptype)
            (bc $kb $depth (: $prem $ptype))))
     (: ($rule $prem) $theorem)))

;; ---- Chain + noise knowledge base ------------------------------------------
!(bind! &kb (new-space))
!(add-atom &kb (: seed (Reach g0)))
PRELUDE

for ((i=0; i<DEPTH; i++)); do
  j=$((i+1))
  echo "!(add-atom &kb (: reach_${i} (-> (: \$p (Reach g${i})) (Reach g${j}))))"
  for ((b=0; b<BRANCH; b++)); do
    echo "!(add-atom &kb (: noise_${i}_${b} (-> (: \$p (Reach g${i})) (Noise n_${i}_${b}))))"
  done
done

cat <<QUERY

;; ---- Target query: prove (Reach g${DEPTH}) at depth ${DEPTH} ----------------
;; Contract: derivable (exactly one backward proof: the reach_* spine).
!(bc &kb (fromNumber ${DEPTH}) (: \$prf (Reach g${DEPTH})))
QUERY
