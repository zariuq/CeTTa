; A finite authored history earns and reuses its proof records. Chunked
; execution compares complete returned states, including caches and receipts.

(= (mixed-history:first)
  (HOL:Step "poly-refl" (REFL (Var "value" (Tyvar "A")))))

(= (mixed-history:rest)
  (HOL:Cons (HOL:Step "shared-trans" (TRANS "poly-refl" "poly-refl"))
    (HOL:Cons (HOL:Step "bool-instance"
      (INST_TYPE (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) HOL:Nil)
        "shared-trans")) HOL:Nil)))

(= (mixed-history:commands)
  (let $first (mixed-history:first)
    (let $rest (mixed-history:rest) (HOL:Cons $first $rest))))

(= (mixed-history:whole)
  (let $commands (mixed-history:commands) (hol-light:run HOL:Nil $commands)))

(= (mixed-history:chunked)
  (let $first (mixed-history:first)
    (case (hol-light:run HOL:Nil (HOL:Cons $first HOL:Nil))
      (((Some $state) (hol-light:run-from $state (mixed-history:rest)))
       ($refused None)))))

(= (mixed-history:equation $type)
  (Comb (Comb (Const "=" (hol-light:eq-type $type)) (Var "value" $type))
    (Var "value" $type)))

(= (mixed-history:receipts)
  (case (mixed-history:whole)
    (((Some (HOL:State $operators $constants $axioms $cache $receipts))
      (== $receipts
        (HOL:Cons (HOL:Receipt "bool-instance"
          (INST_TYPE (HOL:Cons (HOL:Maplet (Tyapp "bool" HOL:Nil) (Tyvar "A")) HOL:Nil)
            "shared-trans") (HOL:Cons "shared-trans" HOL:Nil))
          (HOL:Cons (HOL:Receipt "shared-trans" (TRANS "poly-refl" "poly-refl")
            (HOL:Cons "poly-refl" (HOL:Cons "poly-refl" HOL:Nil)))
            (HOL:Cons (HOL:Receipt "poly-refl" (REFL (Var "value" (Tyvar "A"))) HOL:Nil)
              HOL:Nil)))))
     ($refused False))))

(= (mixed-history:repeated-key)
  (case (mixed-history:whole)
    (((Some $state) (hol-light:step $state "poly-refl" (REFL (Var "value" (Tyvar "A")))))
     ($refused False))))

!(MixedHistoryControl earned-boolean
  (== (hol-light:check-result (mixed-history:whole) "bool-instance" HOL:Nil
    (mixed-history:equation (Tyapp "bool" HOL:Nil))) True))
!(MixedHistoryControl retained-polymorphic-premise
  (== (hol-light:check-result (mixed-history:whole) "poly-refl" HOL:Nil
    (mixed-history:equation (Tyvar "A"))) True))
!(MixedHistoryControl exact-ordered-receipts (== (mixed-history:receipts) True))
!(MixedHistoryControl complete-state-resumption (== (mixed-history:chunked) (mixed-history:whole)))
!(MixedHistoryControl occupied-key-refused (== (mixed-history:repeated-key) None))
!(MixedHistoryControl missing-premise-refused
  (== (hol-light:run HOL:Nil
    (HOL:Cons (HOL:Step "missing-trans" (TRANS "absent" "absent")) HOL:Nil)) None))
!(MixedHistoryControl wrong-resumption-state-refused
  (let $rest (mixed-history:rest) (== (hol-light:run HOL:Nil $rest) None)))
!(MixedHistoryControl wrong-type-claim-refused
  (== (hol-light:check-result (mixed-history:whole) "bool-instance" HOL:Nil
    (mixed-history:equation (Tyvar "A"))) False))
