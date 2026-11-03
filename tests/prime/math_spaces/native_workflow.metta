!(import! &self math_space)
!(bind! &research (math:new "arithmetic"))
!(assertEqual
  (math:declare &research (quote (hol:inductive Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))))
  (Established (Nat)))
!(assertEqual
  (math:declare &research (quote (hol:define add (-> Nat Nat Nat)
     (= (add $n Z) $n) (= (add $n (S $m)) (S (add $n $m))))))
  (Established (add)))
!(assertEqual (math:run &research (quote (add (S Z) (S Z)))) (S (S Z)))
!(assertEqual
  (math:goal &research "right-zero" type
     (quote (-> (n : Nat) (id Nat (add n Z) n))) "Addition preserves its left input at zero.")
  (Math:GoalAdded "right-zero"))
!(assertEqual
  (math:goal &research "right-zero" type (quote Nat) "A conflicting use of the same goal ID.")
  (Math:Conflict goal "right-zero"))
!(assertEqual
  (math:propose &research "wrong" "right-zero" "reflexivity" (quote (lam n (refl (S n)))))
  (Math:Proposed "wrong" "right-zero"))
!(let $_ (math:check &research "wrong" 100000)
   (assertEqual (math:attempt-status &research "wrong") (rejected (reflexivity-endpoint-mismatch))))
!(assertEqual (math:goal-status &research "right-zero" 100000)
   (Math:GoalStatus "right-zero" open no-current-publication))

; A strategy is an ordinary MeTTa function producing proof syntax as data.
(= (right-zero-strategy) (quote (lam n (refl n))))
!(assertEqual
  (math:propose &research "right" "right-zero" "reflexivity" (right-zero-strategy))
  (Math:Proposed "right" "right-zero"))
!(let $_ (math:check &research "right" 100000)
   (assertEqual (math:attempt-status &research "right") checked))
!(assertEqual (math:publish &research "wrong" wrong-lemma 100000)
   (Math:PublicationFailed wrong-lemma (Refuted (reflexivity-endpoint-mismatch))))
!(assertEqual (math:publish &research "right" right-zero 100000)
   (Math:Published right-zero "right"))
!(assertEqual (math:goal-status &research "right-zero" 100000)
   (Math:GoalStatus "right-zero" checked (published right-zero)))
!(assertEqual
  (math:goal &research "concrete" type (quote (id Nat (add (S Z) Z) (S Z))) "Reuse the universal result.")
  (Math:GoalAdded "concrete"))
!(let $_ (math:attempt &research "reuse" "concrete" "known-result" (quote (right-zero (S Z))) 100000)
   (assertEqual (math:attempt-status &research "reuse") checked))
!(let $_ (math:attempt &research "limited" "concrete" "known-result" (quote (right-zero (S Z))) 0)
   (assertEqual (math:attempt-status &research "limited") (unfinished (checking-budget 0))))
!(assertEqual
  (math:declare &research (quote (hol:axiom cheat (all prop (lam p p)))))
  (Math:Refused declaration-head hol:axiom))
!(assertEqual (math:propose &research "raw" "concrete" "raw-code" (refl Z))
   (Math:Refused unquoted-proof "raw"))
!(println! (MathSpaceNativeSummary 18 18 0))
