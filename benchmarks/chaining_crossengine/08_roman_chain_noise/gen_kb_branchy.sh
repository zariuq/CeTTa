#!/usr/bin/env bash
# Row 08b — DERIVED high-multiplicity variant of the chain (NOT the original
# PeTTaChainer benchmark; a probe we built to isolate solution multiplicity).
# Unlike gen_kb.sh (noise concludes Noise, so backward is linear with 1 proof),
# here each step gets B *alternative* rules that also conclude (Reach g_{i+1})
# from the same premise (Reach g_i). Backward chaining then branches: the target
# (Reach gD) has (1+B)^D distinct proofs. This isolates the question of whether
# the CeTTa-vs-PeTTa gap is driven by nondeterministic solution multiplicity
# rather than per-step dispatch cost. Correctness contract: both engines must
# report the SAME proof count (1+B)^D; timing then reflects per-solution cost.
#
# Usage: gen_kb_branchy.sh DEPTH BRANCHING > out.metta   (proof count = (1+B)^DEPTH)
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

;; ---- Branchy chain: each step has 1 real + B alternative rules -------------
!(bind! &kb (new-space))
!(add-atom &kb (: seed (Reach g0)))
PRELUDE

for ((i=0; i<DEPTH; i++)); do
  j=$((i+1))
  echo "!(add-atom &kb (: reach_${i} (-> (: \$p (Reach g${i})) (Reach g${j}))))"
  for ((b=0; b<BRANCH; b++)); do
    echo "!(add-atom &kb (: alt_${i}_${b} (-> (: \$p (Reach g${i})) (Reach g${j}))))"
  done
done

cat <<QUERY

;; ---- Target query: all proofs of (Reach g${DEPTH}); count = (1+${BRANCH})^${DEPTH} ----
!(bc &kb (fromNumber ${DEPTH}) (: \$prf (Reach g${DEPTH})))
QUERY
