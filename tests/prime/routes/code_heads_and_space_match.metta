; Equation heads and space queries take code apart binder by binder, never
; by spelling (stage 5, item 2), as let, unify, case and switch do
; (contextual_code.metta).  Candidates are selected as before, with the code
; that holds binders read as a variable by the indexes, since its binders'
; spellings decide nothing; each candidate is then matched binder by
; binder, and a part under binders of the code it mentions is contextual
; code.
(= (body-of (quote (lam $x $b))) (Got $x $b))
(= (is-identity (quote (lam w w))) yes)
(= (body-quoted (quote (lam $x $b))) (quote (lam $x (g $b))))

; ---- Equation heads.
!(row head-contextual)
!(body-of @(lam z (f z)))
!(row head-contextual-lift-let)
!(let (Got $x $b) (body-of @(lam z (f z))) (lift let z @7 $b))
!(row head-spliced-back)
!(body-quoted @(lam y (f y)))
!(row head-alpha)
!(is-identity @(lam z z))
!(row head-free-is-not-bound)
!(is-identity @(lam w z))
!(row head-closed-part)
!(body-of @(lam z (f c)))

; ---- Many equations of one head: the indexes select among them.
(= (shape (quote (lam w w))) identity)
(= (shape (quote (lam w (f w)))) applied)
(= (shape (quote (lam w c))) constant)
(= (shape (quote (lam v (lam w v)))) first)
(= (shape (quote (lam v (lam w w)))) second)
(= (shape (quote (g a))) other)
!(row indexed-identity)
!(shape @(lam z z))
!(row indexed-first)
!(shape @(lam a (lam b a)))
!(row indexed-second)
!(shape @(lam a (lam b b)))
!(row indexed-applied)
!(shape @(lam q (f q)))
!(row indexed-none)
!(shape @(lam q (f c)))

; ---- Space queries.
!(add-atom &self (stored (quote (lam z z))))
!(add-atom &self (kept (quote (lam z (f z)))))
!(row space-alpha)
!(match &self (stored (quote (lam w w))) alpha)
!(row space-free-is-not-bound)
!(match &self (stored (quote (lam w z))) wrong)
!(row space-contextual)
!(match &self (kept (quote (lam $x $b))) (Got $x $b))

; ---- Inside equation bodies, where the relational machine runs the
; equation: the forms and calls that take code apart are matched binder by
; binder there too.
(= (via-let $c) (let (quote (lam w w)) $c yes))
(= (via-unify $c) (unify $c (quote (lam w w)) yes no))
(= (via-case $c) (case $c (((quote (lam w w)) yes) ($_ no))))
(= (via-match) (match &self (stored (quote (lam w w))) yes))
(= (via-call $c) (is-identity $c))
!(row body-let)
!(via-let @(lam z z))
!(row body-unify)
!(via-unify @(lam z z))
!(row body-case)
!(via-case @(lam z z))
!(row body-case-free)
!(via-case @(lam w z))
!(row body-match)
!(via-match)
!(row body-call)
!(via-call @(lam z z))
