; The smt library on fixed problems: congruence, transitivity, a predicate
; through congruence, the f^3(a) = a, f^5(a) = a lemma, a satisfiable choice
; between equalities, and propositions among equality atoms.
!(import! &self smt)
; a = b, f(a) != f(b): unsat by congruence
!(smt:solve ((clause ((eq a b)) ()) (clause () ((eq (f a) (f b))))))
; a = b, b = c, a != c: unsat by transitivity
!(smt:solve ((clause ((eq a b)) ()) (clause ((eq b c)) ()) (clause () ((eq a c)))))
; p(a), a = b, not p(b): unsat, congruence on a predicate
!(smt:solve ((clause ((p a)) ()) (clause ((eq a b)) ()) (clause () ((p b)))))
; f(f(f(a))) = a, f(f(f(f(f(a))))) = a, f(a) != a: unsat (classic)
!(smt:solve ((clause ((eq (f (f (f a))) a)) ()) (clause ((eq (f (f (f (f (f a))))) a)) ()) (clause () ((eq (f a) a)))))
; a = b or a = c; b != c; f(a) != f(b): sat, a = c
!(smt:solve ((clause ((eq a b) (eq a c)) ()) (clause () ((eq b c))) (clause () ((eq (f a) (f b))))))
; propositions mixed in
!(smt:solve ((clause (q (eq a b)) ()) (clause () (q)) (clause () ((eq (g a) (g b))))))
; SMT-LIB text (QF_UF): let, a term ite, distinct, define-fun, xor, =>, Boolean
; equality, and a satisfiable choice, each verdict equal to cvc5's.
!(let $r (smt:solve-text "(set-logic QF_UF)(declare-sort U 0)(declare-fun a () U)(declare-fun b () U)(declare-fun c () U)(declare-fun f (U) U)(declare-fun p (U) Bool)(declare-fun q () Bool)(assert (let ((x (f a)) (y (f b))) (and (= a b) (not (= x y)))))") (if (== $r sat) sat (car-atom $r)))
!(let $r (smt:solve-text "(set-logic QF_UF)(declare-sort U 0)(declare-fun a () U)(declare-fun b () U)(declare-fun c () U)(declare-fun f (U) U)(declare-fun p (U) Bool)(declare-fun q () Bool)(assert (= c (ite q a b)))(assert (not (= c a)))(assert (not (= c b)))") (if (== $r sat) sat (car-atom $r)))
!(let $r (smt:solve-text "(set-logic QF_UF)(declare-sort U 0)(declare-fun a () U)(declare-fun b () U)(declare-fun c () U)(declare-fun f (U) U)(declare-fun p (U) Bool)(declare-fun q () Bool)(assert (distinct a b c))(assert (= (f a) (f b)))") (if (== $r sat) sat (car-atom $r)))
!(let $r (smt:solve-text "(set-logic QF_UF)(declare-sort U 0)(declare-fun a () U)(declare-fun b () U)(declare-fun c () U)(declare-fun f (U) U)(declare-fun p (U) Bool)(declare-fun q () Bool)(define-fun g ((x U)) U (f (f x)))(assert (= (g a) a))(assert (not (= (f (f (f (f a)))) a)))") (if (== $r sat) sat (car-atom $r)))
!(let $r (smt:solve-text "(set-logic QF_UF)(declare-sort U 0)(declare-fun a () U)(declare-fun b () U)(declare-fun c () U)(declare-fun f (U) U)(declare-fun p (U) Bool)(declare-fun q () Bool)(assert (xor q (p a)))(assert (= q (p b)))(assert (= a b))") (if (== $r sat) sat (car-atom $r)))
!(let $r (smt:solve-text "(set-logic QF_UF)(declare-sort U 0)(declare-fun a () U)(declare-fun b () U)(declare-fun c () U)(declare-fun f (U) U)(declare-fun p (U) Bool)(declare-fun q () Bool)(assert (=> (p a) (= b c)))(assert (p a))(assert (not (= (f b) (f c))))") (if (== $r sat) sat (car-atom $r)))
!(let $r (smt:solve-text "(set-logic QF_UF)(declare-sort U 0)(declare-fun a () U)(declare-fun b () U)(declare-fun c () U)(declare-fun f (U) U)(declare-fun p (U) Bool)(declare-fun q () Bool)(assert (= q (= a b)))(assert q)(assert (not (p a)))(assert (p b))") (if (== $r sat) sat (car-atom $r)))
!(let $r (smt:solve-text "(set-logic QF_UF)(declare-sort U 0)(declare-fun a () U)(declare-fun b () U)(declare-fun c () U)(declare-fun f (U) U)(declare-fun p (U) Bool)(declare-fun q () Bool)(assert (or (= a b) (= a c)))(assert (not (= (f a) (f b))))") (if (== $r sat) sat (car-atom $r)))
; Linear real arithmetic: bound atoms (le A B) (lt A B) (ge A B) (gt A B)
; over add, sub, mul by constants and div by a nonzero constant; models give
; each real an exact value, refutations hold (lra ID CLAUSE WEIGHTS) steps,
; Farkas coefficients the checker verifies exactly.
!(smt:solve ((clause ((le (add x y) 2)) ()) (clause ((ge (sub x y) 3)) ())))
!(smt:solve ((clause ((le (add x y) 2)) ()) (clause ((ge (sub x y) 3)) ()) (clause ((ge y 0)) ())))
!(smt:solve ((clause ((lt x 1)) ()) (clause ((gt (mul 2 x) 2)) ())))
!(smt:solve ((clause ((lt x 1) (gt x 1)) ()) (clause ((le x 1)) ()) (clause ((ge x 1)) ())))
!(smt:solve ((clause ((le (mul 3 x) 1)) ()) (clause ((ge (mul 3 x) 1)) ()) (clause ((le x (div 1 3))) ())))
!(smt:solve ((clause (p) ()) (clause ((ge x 5)) (p)) (clause ((le x -5) p) ()) (clause ((le x 1)) ()) (clause ((ge x -1)) ())))
!(smt:solve ((clause ((le (mul x y) 1)) ())))
!(smt:solve ((clause ((le x 1.5)) ())))
!(smt:solve ((clause ((eq x a)) ()) (clause ((le x 1)) ())))
; SMT-LIB text (QF_LRA): decimals read exactly, term ite through a fresh
; real, Real macros and lets, distinct over reals; each verdict equal to cvc5's.
!(smt:solve-text "(set-logic QF_LRA)(declare-fun x () Real)(declare-fun y () Real)(assert (<= (+ x y) 2.5))(assert (>= (- x y) 3))(check-sat)")
!(smt:solve-text "(set-logic QF_LRA)(declare-fun x () Real)(declare-fun y () Real)(assert (<= (+ x y) 2))(assert (>= (- x y) 3))(assert (>= y 0))(check-sat)")
!(smt:solve-text "(set-logic QF_LRA)(declare-const x Real)(declare-const b Bool)(assert (= (ite b (* 2 x) (/ x 3)) 1))(assert (> x 2))(check-sat)")
!(smt:solve-text "(set-logic QF_LRA)(declare-const x Real)(define-fun twice ((z Real)) Real (* 2 z))(assert (let ((u (twice x))) (and (< u 1) (distinct x 0.25) (> x 0.1))))(check-sat)")
!(smt:solve-text "(set-logic QF_LRA)(declare-const x Real)(assert (and (> x 0.1) (< x 0.10000000000000001)))(check-sat)")
!(smt:solve-text "(set-logic QF_LRA)(declare-const x Real)(assert (and (< x 0.1) (> x 0.10000000000000001)))(check-sat)")
!(smt:solve-text "(set-logic QF_LRA)(declare-const x Real)(declare-const y Real)(assert (< (* x y) 1))(check-sat)")
!(smt:solve-text "(set-logic QF_LRA)(declare-const x Real)(assert (< (+ x) 1))(check-sat)")
; uninterpreted functions over reals: a bound on f(x), and x <= y <= x
; forcing f(x) = f(y)
!(smt:solve-text "(set-logic QF_UFLRA)(declare-fun f (Real) Real)(declare-const x Real)(assert (< (f x) 1))(check-sat)")
!(car-atom (smt:solve-text "(set-logic QF_UFLRA)(declare-fun f (Real) Real)(declare-const x Real)(declare-const y Real)(assert (<= x y))(assert (<= y x))(assert (< (f x) (f y)))(check-sat)"))
; Optimization: the least or greatest value of an arithmetic term over the
; models, an LP, a credal readout, an infimum not attained, an unbounded term,
; a choice between Boolean branches and no model; each optimum certified by a
; refutation of a better value.
!(smt:minimize ((clause ((ge x 1)) ()) (clause ((ge y 2)) ()) (clause ((le (add x (mul 2 y)) 10)) ())) (add x y))
!(smt:maximize ((clause ((ge x 1)) ()) (clause ((ge y 2)) ()) (clause ((le (add x (mul 2 y)) 10)) ())) (add x y))
!(smt:minimize ((clause ((ge p (div 3 10))) ()) (clause ((ge q (div 1 5))) ()) (clause ((le (add p q) 1)) ())) (sub p q))
!(smt:maximize ((clause ((ge p (div 3 10))) ()) (clause ((ge q (div 1 5))) ()) (clause ((le (add p q) 1)) ())) (sub p q))
!(smt:minimize ((clause ((gt x 2)) ())) x)
!(smt:minimize ((clause ((le x 2)) ())) x)
!(smt:minimize ((clause ((ge x 5) b) ()) (clause ((ge x 3)) ()) (clause ((ge c 1)) (b)) (clause ((ge c 0)) ())) (add x (mul 4 c)))
!(smt:minimize ((clause ((ge x 1)) ()) (clause ((le x 0)) ())) x)
