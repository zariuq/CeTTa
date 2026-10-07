; scope_lib_formed_names.metta with every variable and every binder of
; every clause spelled differently; the public names are kept
; (tests/prime/routes/formed_private_names_renamed.metta).
!(import! &self template)
(= (lib-cons-closure) (cons-atom lam (p (new ($r) (let $r p (g $r))))))
(= (lib-star-closure) *@(lam p (let $r p (g $r))))
(= (lib-lift-let-closure) *(lift let m @p @(lam m (let $r m (g $r)))))
(= (lib-cons-cross) (cons-atom lam (a (cons-atom lam (b (let $y b (Pair $y a)))))))
(= (lib-star-cross) *@(lam a (lam b (let $y b (Pair $y a)))))
(= (lib-lift-let-cross)
   *(lift let (c d) (@a @b) @(lam c (lam d (let $y d (Pair $y c))))))
(= (lib-template $list) (template:map $list n @((lam q (let $v q (Pair $v $v))) n)))
