(hol:profile base)
!(import! &self judgments.metta)
!(hol:inductive &self Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))
!(hol:define &self inc (-> Nat Nat) (= (inc $n) (S $n)))
; Direct nesting and staged use must have the same checked result.
!(assertEqual (judge (congruence-judgment inc Nat Nat)) True)
!(let $goal (congruence-judgment inc Nat Nat) (assertEqual (judge $goal) True))
; Inspecting a request first must not replace its syntax by its answer.
!(let* (($goal (congruence-judgment inc Nat Nat)) ($view (inspect-judgment $goal)))
   (assertEqual (judge $goal) True))
!(assertEqual (judge (congruence-judgment inc Nat Nat)) True)
