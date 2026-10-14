!(import! &self ../../lib/hol_light/data.metta)
!(import! &self ../../lib/hol_light/syntax.metta)
!(import! &self ../../lib/hol_light/kernel.metta)
!(import! &self ../../lib/hol_light/definitions.metta)
!(import! &self ../../lib/hol_light/derived.metta)
!(import! &self ../../lib/hol_light/profile.metta)
!(import! &self ../../lib/hol_light/receive.metta)

(= (axiom-event:p) (Var "p" (hol-light:bool)))
(= (axiom-event:q) (Var "q" (hol-light:bool)))
(= (axiom-event:world $term)
  (hol-light:initial (HOL:Cons (HOL:Entry "selected" $term) HOL:Nil)))
(= (axiom-event:run $world $source)
  (hol-light:step $world "event" (AXIOM "selected" $source)))
(= (axiom-event:accepted)
  (axiom-event:run (axiom-event:world (axiom-event:p)) (axiom-event:p)))
(= (axiom-event:has-state $result)
  (case $result (((Some (HOL:State $op $c $a $cache $receipts)) True) ($other False))))
(= (axiom-event:cache-occupied $world)
  (case $world
    (((HOL:State $op $c $a $cache $receipts)
      (HOL:State $op $c $a (HOL:Cons (HOL:Entry "event" opaque-cache-value) $cache) $receipts)))))
(= (axiom-event:receipt-occupied $world)
  (case $world
    (((HOL:State $op $c $a $cache $receipts)
      (HOL:State $op $c $a $cache (HOL:Cons (HOL:Receipt "event" opaque-command HOL:Nil) $receipts))))))
(= (axiom-event:duplicate)
  (case (axiom-event:accepted)
    (((Some $world) (axiom-event:run $world (axiom-event:p))) ($other malformed-result))))
(= (axiom-event:retained)
  (case (axiom-event:accepted)
    (((Some (HOL:State $op $c $a $cache $receipts))
      (and (== $a (HOL:Cons (HOL:Entry "selected" (axiom-event:p)) HOL:Nil))
        (and (== $receipts (HOL:Cons (HOL:Receipt "event" (AXIOM "selected" (axiom-event:p)) HOL:Nil) HOL:Nil))
          (== $cache (HOL:Cons (HOL:Entry "event"
            (HOL:Thm HOL:Nil (HOL:Free "p" (hol-light:bool))
              (HOL:Cons (HOL:AxiomDependency "selected" (HOL:Free "p" (hol-light:bool))) HOL:Nil)
              (HOL:Origin "event" (AXIOM "selected" (axiom-event:p))))) HOL:Nil)))))
     ($other False))))

!(AxiomEventControl accepted (== (axiom-event:has-state (axiom-event:accepted)) True))
!(AxiomEventControl formula-mismatch
  (== (axiom-event:run (axiom-event:world (axiom-event:p)) (axiom-event:q)) None))
!(AxiomEventControl non-boolean
  (== (axiom-event:run (axiom-event:world (Var "x" (Tyvar "a"))) (Var "x" (Tyvar "a"))) None))
!(AxiomEventControl undeclared
  (== (axiom-event:run (hol-light:initial HOL:Nil) malformed-source) None))
!(AxiomEventControl occupied-cache
  (== (axiom-event:run (axiom-event:cache-occupied (axiom-event:world (axiom-event:p))) (axiom-event:p)) None))
!(AxiomEventControl occupied-receipt
  (== (axiom-event:run (axiom-event:receipt-occupied (axiom-event:world (axiom-event:p))) (axiom-event:p)) None))
!(AxiomEventControl duplicate-event (== (axiom-event:duplicate) None))
!(AxiomEventControl payload-and-receipt (== (axiom-event:retained) True))
!(AxiomEventControl first-commitment
  (== (axiom-event:run (hol-light:initial
    (HOL:Cons (HOL:Entry "selected" (axiom-event:q))
      (HOL:Cons (HOL:Entry "selected" (axiom-event:p)) HOL:Nil))) (axiom-event:p)) None))
