; A faster machine's transition count is not Prime's declared fuel metric.
; Under bounded fuel the relation-plan probe must remain canonical.

!(bind! &plan-fuel-space (new-space))
!(add-atom &plan-fuel-space (value done))

(= (plan-fuel-walk Z)
   (match &plan-fuel-space (value $value) $value))
(= (plan-fuel-walk (S $rest))
   (plan-fuel-walk $rest))

!(plan-fuel-walk
   (S (S (S (S (S (S (S (S
   (S (S (S (S (S (S (S (S Z))))))))
   )))))))))
