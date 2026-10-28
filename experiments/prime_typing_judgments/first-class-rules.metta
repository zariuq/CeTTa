(hol:profile base)
!(import! &self judgments.metta)
!(hol:inductive &self Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))
!(hol:define &self inc (-> Nat Nat) (= (inc $n) (S $n)))
; A program builds a contextual rule and then consumes its proof function.
; No theorem name or new conversion policy is installed for this use.
!(let* (($goal (congruence-judgment inc Nat Nat)) ($rule (rule-code $goal)))
   (let (TypeNormalForm $source $value $source-type $result-type)
        (run-rule $rule (quote (Z Z (refl Z))))
     (assertEqual (=alpha $value (refl (app S Z))) True)))
!(let* (($goal (congruence-judgment inc Nat Nat)) ($rule (rule-code $goal)))
   (assertEqual (car-atom (rule-outcome $rule (quote (Z (S Z) (refl Z))))) Refuted))
; A rule-shaped record does not carry a checking privilege.
!(assertEqual (car-atom (rule-outcome
   (quote (RuleProgram (lam n (refl (S n))) (-> (n : Nat) (id Nat n (S n)))))
   (quote (Z)))) Refuted)

; The same rule value can be reused at another correctly indexed input.
!(let* (($goal (congruence-judgment inc Nat Nat)) ($rule (rule-code $goal)))
   (let (TypeNormalForm $source $value $source-type $result-type)
        (run-rule $rule (quote ((S Z) (S Z) (refl (S Z)))))
     (assertEqual (type:check $value (id Nat (inc (S Z)) (inc (S Z)))) True)))
; Unsupported formation does not turn into a mathematical refutation.
!(assertEqual (car-atom (rule-outcome
   (quote (RuleProgram (lam (n : Mystery) n) (-> Mystery Mystery)))
   (quote (Z)))) Undetermined)
