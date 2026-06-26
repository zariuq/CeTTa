#!/usr/bin/env bash
# Row 08c — FORWARD lane over Roman's chain+noise KB (the forward branching axis).
# Same KB as gen_kb.sh, but a forward chainer (fc) saturating from seed:(Reach g0).
# Here the noise BITES: each (Reach gi) fans out to 1 real successor + B dead-end
# (Noise n_i_b) derivations, so forward fan-out grows with branching B — the
# complement of the backward lane (which ignores noise). Contract: both engines
# enumerate the same forward-derivation multiset; metric = time/count vs B.
#
# Usage: gen_kb_forward.sh DEPTH BRANCHING > out.metta
set -euo pipefail
DEPTH="${1:?depth}"
BRANCH="${2:?branching}"

cat <<'PRELUDE'
;; ---- Nat + dependent-typed forward chainer (Roman rule encoding) -----------
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(: fc (-> $a Nat $b $b))
;; emit the current fact
(= (fc $kb $_ (: $prf $type)) (: $prf $type))
;; apply one forward rule then recurse (nondeterministic over matching rules)
(= (fc $kb (S $k) (: $prf $type))
   (let (: $r (-> (: $x $type) $concl))
        (match $kb (: $r (-> (: $x $type) $concl)) (: $r (-> (: $x $type) $concl)))
     (fc $kb $k (: ($r $prf) $concl))))

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

;; ---- Forward-chain from seed for ${DEPTH} steps; enumerate all derivations --
!(fc &kb (fromNumber ${DEPTH}) (: seed (Reach g0)))
QUERY
