; A lambda applied to an argument binds it as an equation call and the
; canonical (App f x) do: one Need cell, shared by every use of the parameter
; (tests/prime/need_application.metta).  An argument with two answers, used
; twice, gives two pairs, not four; its effect runs once; an argument the body
; never uses never runs.  Two binders take two arguments, each a cell of its
; own, so their choices are independent.

(= (pick) heads)
(= (pick) tails)
(= (pp $x) (P $x $x))

; An equation and the canonical application.
!(sort-atom (collapse (pp (pick))))
!(sort-atom (collapse (App (Lam T (P (idx 0) (idx 0))) (pick))))

; The lambda, with a name binder and with a variable binder.
!(sort-atom (collapse ((lam x (P x x)) (pick))))
!(sort-atom (collapse ((lam $x (P $x $x)) (superpose (heads tails)))))

; Two binders, two independent arguments.
!(sort-atom (collapse ((lam x (lam y (P x y))) (pick) (pick))))

; The argument's effect runs once; an unused argument never runs.
!(collapse ((lam x (P x x)) (let $u (println! lambda-argument-ran) ran)))
!(collapse ((lam x done) (let $u (println! never-runs) ran)))
