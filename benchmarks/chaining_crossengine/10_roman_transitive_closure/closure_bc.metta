;; Row 10 — Roman Treutlein: transitive-closure problem (edge -> path).
;; Problem from repos/PeTTaChainer/pettachainer/metta/tests/test_forward_chainer.metta
;;   (edges A->B->C; edgeToPath; pathStep: Path & Edge -> Path; target Path A C).
;; PeTTaChainer's native lane uses compileadd/forward-chain (engine-specific, not
;; HE-portable); this is the faithful HE port of the *problem*, run with a
;; nilbc-style dependent-typed backward chainer on both CeTTa (--lang he) and PeTTa.
;; Contract: (Path A C) is derivable.

;; ---- Nat ----
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

;; ---- Backward chainer (unary + binary rules) ----
(: bc (-> $a Nat $b $b))
(= (bc $kb $_ (: $proof $theorem))
   (match $kb (: $proof $theorem) (: $proof $theorem)))
(= (bc $kb (S $d) (: ($r $p1) $thm))
   (let* (((: $r (-> (: $p1 $t1) $thm)) (bc $kb $d (: $r (-> (: $p1 $t1) $thm))))
          ((: $p1 $t1) (bc $kb $d (: $p1 $t1))))
     (: ($r $p1) $thm)))
(= (bc $kb (S $d) (: ($r $p1 $p2) $thm))
   (let* (((: $r (-> (: $p1 $t1) (: $p2 $t2) $thm)) (bc $kb $d (: $r (-> (: $p1 $t1) (: $p2 $t2) $thm))))
          ((: $p1 $t1) (bc $kb $d (: $p1 $t1)))
          ((: $p2 $t2) (bc $kb $d (: $p2 $t2))))
     (: ($r $p1 $p2) $thm)))

;; ---- Edge/Path knowledge base ----
!(bind! &kb (new-space))
!(add-atom &kb (: eAB (Edge A B)))
!(add-atom &kb (: eBC (Edge B C)))
!(add-atom &kb (: edgeToPath (-> (: $e (Edge $x $y)) (Path $x $y))))
!(add-atom &kb (: pathStep (-> (: $e1 (Path $x $y)) (: $e2 (Edge $y $z)) (Path $x $z))))

;; ---- Query: prove Path A C ----
!(bc &kb (fromNumber 3) (: $prf (Path A C)))
