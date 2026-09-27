; A fact used by (pf:by ...) is instantiated by matching its conclusion
; against the goal.  Matching is first order, so it can miss an instance that
; exists up to conversion, through a higher-order solution, or by computing
; a definition at an instantiated argument.  A proof by a fact is refuted
; only when no instance can exist: two different constructors or constants
; meet where conversion compares arguments one by one, or one quantified
; variable must equal two such different terms.  When the goal fixes every
; quantified variable at such a position, that one instance is decided by
; conversion.  Anything else is left undetermined.
!(import! &self pf)
!(bind! &thy (new-space))
!(set:inductive &thy nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(pf:equality &thy nat refl@nat subst@nat)
!(set:define &thy add (-> nat nat nat)
   (= (add $n zero) $n)
   (= (add $n (succ $m)) (succ (add $n $m))))
!(pf:theory &thy ((: Q (-> nat prop)) (: R (-> nat nat prop)) (: a nat) (: b nat)
                  (: s (-> nat nat)) (: T (-> (-> nat nat) prop))))
!(set:axiom &thy q-succ (all nat (lam x (Q (succ x)))))
!(set:axiom &thy q-add (all nat (lam x (Q (add zero x)))))
!(set:axiom &thy r-diag (all nat (lam x (R x x))))
!(set:axiom &thy r-add (all nat (lam x (R x (add x x)))))
!(set:axiom &thy t-eta (T (lam x (s x))))
!(set:axiom &thy motive (all (-> nat prop) (lam P (imp (all nat (lam n (P n))) (P zero)))))

; Established: a first-order instance.
!(set:proves &thy (pf:by q-succ) (Q (succ zero)))
!(try (set:proves &thy (pf:by q-succ) (Q (succ zero))))
; Established: no quantifier to instantiate; conversion knows eta.
!(set:proves &thy (pf:by t-eta) (T s))
; Established: the goal fixes x at a, and the instance computes to the goal.
!(set:proves &thy (pf:by r-add) (R a (add a a)))
!(set:proves &thy (pf:by r-add) (R (succ zero) (succ (succ zero))))

; True, but not a first-order instance: x = succ zero gives
; Q (add zero (succ zero)), which computes to the goal.  Undetermined.
!(set:proves &thy (pf:by q-add) (Q (succ zero)))
!(try (set:proves &thy (pf:by q-add) (Q (succ zero))))
; True with the motive P = (lam n (eq nat n n)); matching picks the motive
; (eq nat zero), under which the premise fails.  Undetermined.
!(pf:proves &thy (pf:by motive (pf:fix n (pf:refl nat n))) (eq nat zero zero))

; Refuted: succ and zero never meet.
!(set:proves &thy (pf:by q-succ) (Q zero))
!(try (set:proves &thy (pf:by q-succ) (Q zero)))
; Refuted: x would have to be both a and b.
!(set:proves &thy (pf:by r-diag) (R a b))
!(set:proves &thy (pf:by r-diag) (R a a))
; Refuted: the goal fixes x at a, and a + a is not b.
!(set:proves &thy (pf:by r-add) (R a b))
!(try (set:proves &thy (pf:by r-add) (R a b)))
