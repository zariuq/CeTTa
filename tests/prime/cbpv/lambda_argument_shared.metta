; A lambda applied to an argument binds it as a call does.  Equation
; application and canonical ABT application bind an argument as one
; branch-local suspension, demanded where the body uses it
; (tests/prime/need_application.metta).  The lambda row of the binding-site
; table reads the same way, so the elaboration binds the argument by need: an
; argument with two answers, used twice, gives two pairs, not four, and its
; effect runs once.  The rows below state that reading; the draft's lam
; currently substitutes the argument as written instead (by name), which
; gives four pairs and runs the effect twice.
!(import! &self ../../../lib/prime/cbpv.metta)

; The CBPV realization runs on the same evaluator, so the program it reads
; names coin, which has no equations here, where the native rows name pick.
(cbpv:def coin () heads)
(cbpv:def coin () tails)
(= (pick) heads)
(= (pick) tails)
(= (pp $x) (P $x $x))

; The CBPV reading, by need and under an eager bubble (by value).
!(cbpv:elaborated-bag (quote ((s:lam (V x) (P (V x) (V x))) (coin))))
!(cbpv:elaborated-bag (quote (s:demand eager ((s:lam (V x) (P (V x) (V x))) (coin)))))

; Equation application and canonical ABT application.
!(sort-atom (collapse (pp (pick))))
!(sort-atom (collapse (App (Lam T (P (idx 0) (idx 0))) (pick))))

; The lambda as written, with a name binder and with a variable binder.
!(sort-atom (collapse ((lam x (P x x)) (pick))))
!(sort-atom (collapse ((lam $x (P $x $x)) (superpose (heads tails)))))

; The argument's effect runs once.
!(collapse ((lam x (P x x)) (let $u (println! lambda-argument-ran) ran)))

; An argument the body never uses is never run.
!(collapse ((lam x done) (let $u (println! never-runs) ran)))
