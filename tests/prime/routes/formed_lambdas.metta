; Lambdas built while the program runs get their binders' identities when
; they are formed: by cons-atom or union-atom, by opening quoted code with
; `*` (code `lift` built included), by `parse`, and by a substitution that
; puts `lam` at the head of an expression.  Binders carry identities, never
; spellings.  Which scope a store name belongs to was fixed where it was
; written, and forming a lambda never decides it again (stage 5, item 5).
; L = (lam z (let $y z (g $y))).
(= (twice $f) (Pair ($f 1) ($f 2)))

; ---- L written whole: in the program, in code opened by `*`, in text read
; by `parse`, and in code that `lift let` fills.  L owns $y, where it is
; written, so two calls each have their own.
!(row written)
!(twice (lam z (let $y z (g $y))))
!(row star-opened)
!(let $c @(lam z (let $y z (g $y))) (twice *$c))
!(row parse)
!(twice (parse "(lam z (let $y z (g $y)))"))
!(row lift-let)
!(let $c (lift let k @z @(lam k (let $y k (g $y)))) (twice *$c))

; ---- L formed from parts.  $y was written outside any lambda: in the
; query (cons-atom, union-atom, a substitution head), in the activation of
; the lambda whose body wrote it (a beta head), or in code that owns it
; (lift app).  The formed lambda captures that $y, so its two calls share
; one $y and conflict.
!(row cons-atom)
!(twice (cons-atom lam (z (let $y z (g $y)))))
!(row union-atom)
!(twice (union-atom (lam z) ((let $y z (g $y)))))
!(row lift-app)
!(let $c (lift app @lam @z @(let $y z (g $y))) (twice *$c))
!(row substitution-head)
!(let $h lam (twice ($h z (let $y z (g $y)))))
!(row beta-head)
!((lam h (twice (h z (let $y z (g $y))))) lam)

; ---- A crossing set written in the code shares $y: two calls that disagree
; conflict, two that agree answer.
!(row star-opened-crossing)
!(let $c (quote (lam z (let $y z (g $y))){$y}) (twice *$c))
!(row star-opened-crossing-agree)
!(let $c (quote (lam z (let $y z (g $y))){$y}) (let $f *$c (Pair ($f 1) ($f 1))))

; ---- Parameters are lexical in a formed lambda: an outside name spelled as
; the parameter is another name.
!(row formed-parameter)
!(let $f (cons-atom lam ($x (g $x))) (Pair ($f 1) $x))

; ---- A value never elaborated again.  A closure that captures $n and owns
; nothing keeps its capture when it is lifted into code and opened, and when
; it is a part of a lambda formed later.
!(row lifted-closure)
!(let $f (lam z (Pair z $n)) (let $c (lift $f) (let $n 5 (*$c 1))))
!(row closure-part)
!(let $g (lam w (Pair w $n)) (let $f (cons-atom lam (z ($g z))) (let $n 5 ($f 1))))

; ---- A formed lambda keeps the scope its parts' names were written in:
; $n, written in the query, is captured whether it is bound after the lambda
; is formed or before (formed_names_keep_scope.metta has every way).
!(row formed-captures-bound-after)
!(let $f (cons-atom lam (z (Pair z $n))) (let $n 5 ($f 1)))
!(row formed-captures-bound-before)
!(let $n 5 (let $f (cons-atom lam (z (Pair z $n))) ($f 1)))
!(row written-captures)
!(let $f (lam z (Pair z $n)) (let $n 5 ($f 1)))
