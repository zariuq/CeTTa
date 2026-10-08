; Rule-M ownership of separately written code parts.
(scope:profile mercury-implicit per-call reference)

; Code formed at run time never decides ownership again (stage 5, item 5).
; A part carries the scope of each store name from where it was written: a
; name written outside a quotation belongs to the scope it was written in,
; and a lambda formed from parts keeps that identity, so it captures the
; name.  The answer is the same whether the name is bound before the lambda
; is formed or after.  To own a name, the formed lambda's body writes `new`,
; or the lambda is written whole, as code whose ownership was decided where
; it was written.

; ---- cons-atom, union-atom, a substitution head and a beta head, each with
; $n bound after the lambda is formed and before.
!(row cons-atom-bound-after)
!(let $f (cons-atom lam (z (Pair z $n))) (let $n 5 ($f 1)))
!(row cons-atom-bound-before)
!(let $n 5 (let $f (cons-atom lam (z (Pair z $n))) ($f 1)))
!(row union-atom-bound-after)
!(let $f (union-atom (lam z) ((Pair z $n))) (let $n 5 ($f 1)))
!(row union-atom-bound-before)
!(let $n 5 (let $f (union-atom (lam z) ((Pair z $n))) ($f 1)))
!(row substitution-head-bound-after)
!(let $h lam (let $f ($h z (Pair z $n)) (let $n 5 ($f 1))))
!(row substitution-head-bound-before)
!(let $n 5 (let $h lam (let $f ($h z (Pair z $n)) ($f 1))))
!(row beta-head-bound-after)
!((lam h (let $f (h z (Pair z $n)) (let $n 5 ($f 1)))) lam)
!(row beta-head-bound-before)
!(let $n 5 ((lam h (let $f (h z (Pair z $n)) ($f 1))) lam))

; ---- The formed lambda captures the name: two calls share it.
!(row formed-captures)
!(let $f (cons-atom lam (z (let $y z (g $y)))) (Pair ($f 1) ($f 2)))
!(row formed-captures-agree)
!(let $f (cons-atom lam (z (let $y z (g $y)))) (Pair ($f 1) ($f 1)))

; ---- Controls: a formed lambda whose body writes `new` owns its local, and
; a lambda written whole in code owns its local wherever the code is opened.
!(row formed-new-owns)
!(let $f (cons-atom lam (z (new ($y) (let $y z (g $y))))) (Pair ($f 1) ($f 2)))
!(row written-in-code-owns)
!(let $f *@(lam z (let $y z (g $y))) (Pair ($f 1) ($f 2)))
