; Evaluation around holes, and fill-and-resume.  A hole is a function symbol
; with no equation yet, applied to the variables its filling may use: (?w x).
; The draft leaves unevaluated what it cannot interpret, so a hole that is
; reached stays in the result with the values of those variables, and
; evaluation goes on around it: a sum with an unevaluated summand stays a
; sum.  Filling a hole is adding its equation; resuming a result is
; evaluating it again.
;
; The programs use the calculus of the metatheory: numbers, booleans, +, if,
; lam and application, with named definitions standing for lambdas, and each
; answer is the one its evaluation rules give (Eval, Eval.deterministic).
; Every filling here terminates.  The draft demands arguments lazily, so a filling
; that loops where it is never demanded still terminates here, unlike under
; the metatheory's call by value; no such filling is used.

; ---- 1. before filling --------------------------------------------------------

; A result that does not depend on the hole (Refinement.guarded_determinate) ...
!(if True 3 (?guard))
; ... and one that does: an indeterminate sum (Refinement.sum_indeterminate).
!(+ (?u) 1)
; One hole reached twice, each time recorded with its own x
; (HoleInstances.eval_program).
!((lam f (+ (f 1) (f 2))) (lam x (+ (?w x) x)))

; ---- 2. filling and resuming --------------------------------------------------

; Fill ?w with x, in the hole's own scope.
!(add-atom &self (= (?w $x) $x))
; Resuming the recorded result evaluates each instance in its own scope
; (HoleInstances.resume_program) ...
!(+ (+ (?w 1) 1) (+ (?w 2) 2))
; ... and gives what the filled program computes (HoleInstances.eval_filled,
; HoleInstances.resume_agrees, Eval.eval_fill_iff_resume).
!((lam f (+ (f 1) (f 2))) (lam x (+ (?w x) x)))

; A determinate result is the result of every terminating completion
; (determinate_of_eval, Refinement.guarded_completions): two fillings, one
; answer.
!(add-atom &self (= (?guard) 7))
!(if True 3 (?guard))
!(remove-atom &self (= (?guard) 7))
!(add-atom &self (= (?guard) False))
!(if True 3 (?guard))
; An indeterminate one is not: filling ?u with 0 and with 1 gives different
; answers (Refinement.indeterminate_outputs_differ).
!(add-atom &self (= (?u) 0))
!(+ (?u) 1)
!(remove-atom &self (= (?u) 0))
!(add-atom &self (= (?u) 1))
!(+ (?u) 1)

; ---- 3. a program with a gap ----------------------------------------------------

; A delivery fee.  Members pay nothing; the rule for everyone else is not
; written yet, and depends on the weight.
(= (fee $member $weight) (if $member 0 (?shipping $weight)))
; What a member pays is already final (determinate_of_eval); a non-member's
; fee is the hole, recorded with the weight (Eval.hole, Eval.deterministic).
!(fee True 7)
!(fee False 7)
; Now the rule is written.
!(add-atom &self (= (?shipping $w) (+ $w 5)))
; The member's fee has not changed (determinate_of_eval), and resuming the
; recorded result gives what the filled program computes
; (Eval.resume_of_eval_fill, Eval.eval_fill_iff_resume).
!(fee True 7)
!(?shipping 7)
!(fee False 7)
