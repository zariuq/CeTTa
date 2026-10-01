; Identity as observation: two boolean functions, the identity and case
; analysis that returns its argument's constructor, compared by two kinds of
; observer.  The eliminators (application to each boolean, then reading the
; result) identify them; an observer that reads code separates them.

!(bind! &thy (new-space))
!(set:inductive &thy bool (u 0) (: tt bool) (: ff bool))
; lam x. if x then tt else ff, as case analysis
!(set:define &thy cond-id (-> bool bool) (= (cond-id tt) tt) (= (cond-id ff) ff))
; lam x. x
!(set:define &thy id-bool (-> bool bool) (= (id-bool $x) $x))

; 1. Every eliminator observation agrees: applied to tt and to ff, the two
;    functions compute the same boolean (eliminators_relEquiv_idBool,
;    obsEq_idBool_condIdBool).
!(set:eq &thy (cond-id tt) (id-bool tt))
!(set:eq &thy (cond-id ff) (id-bool ff))
; and the observation is not trivial: it separates tt from ff
; (obsType_tt_ne_ff).
!(set:eq &thy (cond-id tt) ff)

; 2. An observer that reads code separates them: their codes differ
;    (not_codeObservers_relEquiv_idBool; code_eq_of_codeObservers_relEquiv:
;    code observers identify only terms with equal codes).
!(== (quote (lam x (cond-id x))) (quote (lam x x)))
!(== (quote (lam x x)) (quote (lam x x)))
