; Translated from PeTTa to HE (extended, 13 atoms)
; Source: loowoz-petta-source.metta

!(translatePredicate (set_prolog_flag occurs_check True))
(: Sized (-> $a Type))
(: MkSized (-> $n $a (Sized $a)))
(= (Sized.size $__tr_head_arg_1) (unify $__tr_head_arg_1 (MkSized $s $x) $s Empty))
(= (Sized.object $__tr_head_arg_2) (unify $__tr_head_arg_2 (MkSized $s $x) $x Empty))
(= (obc-gtz $s $__tr_head_arg_3) (unify $__tr_head_arg_3 (: ax₁ (→ $𝜑 (→ $𝜓 $𝜑))) (MkSized 1 (: ax₁ (→ $𝜑 (→ $𝜓 $𝜑)))) Empty))
(= (obc-gtz $s $__tr_head_arg_4) (unify $__tr_head_arg_4 (: ax₂ (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))) (MkSized 1 (: ax₂ (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒))))) Empty))
(= (obc-gtz $s $__tr_head_arg_5) (unify $__tr_head_arg_5 (: ax₃ (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))) (MkSized 1 (: ax₃ (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑)))) Empty))
(= (obc-gtz $s $__tr_head_arg_6) (unify $__tr_head_arg_6 (: (mp $f $x) $b) (if (< 2 $s) (let* (((MkSized $fs (: $f (→ $a $b))) (obc (- $s 2) (: $f (→ $a $b)))) ((MkSized $xs (: $x $a)) (obc (- (- $s 1) $fs) (: $x $a)))) (MkSized (+ (+ $fs $xs) 1) (: (mp $f $x) $b))) (empty)) Empty))
(= (obc $s $__tr_head_arg_7) (unify $__tr_head_arg_7 (: $x $a) (if (< 0 $s) (obc-gtz $s (: $x $a)) (empty)) Empty))
!(obc 15 (: $x (→ (→ (→ 𝜑 𝜓) (→ 𝜑 𝜒)) (→ (→ 𝜓 𝜑) (→ 𝜓 𝜒)))))
