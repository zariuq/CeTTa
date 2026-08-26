;; Row 03 — Nil Geisweiller: non-curried (n-ary) DTL backward chainer (synthesize).
;; Source: repos/ngeiswei-chaining-run/dtl/backward/no-curry.metta (+ utils.metta, inlined).
;; n-ary rule clauses (nullary..quintenary) — fan-out contrast vs row 02 (curried).
;; Contract: depth-3 query (. g $f):(-> $in Bool) yields 5 proofs (upstream test).
;; Runs on CeTTa (--lang he) and PeTTa.

;; ---- Nat ----
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

;; ---- Non-curried backward chainer (synthesize) ----
(: synthesize (-> $a Nat $b $b))
(= (synthesize $kb $depth $query)
     (match $kb $query $query))
(= (synthesize $kb (S $k) $query)
   (let* (((: $ructor (-> $premise $conclusion)) (match $kb $a $a))
          ((: ($ructor $proof) $conclusion) $query)
          ((: $proof $premise) (synthesize $kb $k (: $proof $premise))))
     $query))
(= (synthesize $kb (S $k) $query)
   (let* (((: $ructor (-> $premise1 $premise2 $conclusion)) (match $kb $a $a))
          ((: ($ructor $proof1 $proof2) $conclusion) $query)
          ((: $proof1 $premise1) (synthesize $kb $k (: $proof1 $premise1)))
          ((: $proof2 $premise2) (synthesize $kb $k (: $proof2 $premise2))))
     $query))
(= (synthesize $kb (S $k) $query)
   (let* (((: $ructor (-> $premise1 $premise2 $premise3 $conclusion)) (match $kb $a $a))
          ((: ($ructor $proof1 $proof2 $proof3) $conclusion) $query)
          ((: $proof1 $premise1) (synthesize $kb $k (: $proof1 $premise1)))
          ((: $proof2 $premise2) (synthesize $kb $k (: $proof2 $premise2)))
          ((: $proof3 $premise3) (synthesize $kb $k (: $proof3 $premise3))))
     $query))

;; ---- Knowledge + rule base ----
!(bind! &kb (new-space))
!(add-atom &kb (: f (-> Number String)))
!(add-atom &kb (: g (-> String Bool)))
!(add-atom &kb (: h (-> Bool Number)))
!(add-atom &kb (: i (-> String Number Bool)))
!(add-atom &kb (: . (-> (-> $b $c) (-> $a $b) (-> $a $c))))
!(add-atom &kb (: .: (-> (-> $c $d) (-> $a $b $c) (-> $a $b $d))))

;; ---- Query: depth-3 functions composable with g returning Bool (5 proofs) ----
!(synthesize &kb (fromNumber 3) (: (. g $f) (-> $in Bool)))
