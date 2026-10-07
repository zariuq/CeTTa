; A library for the formed-name invariance fixtures
; (tests/prime/routes/formed_private_names.metta): closures formed while the
; program runs, by cons-atom, by opening written code with `*`, and by
; opening code that `lift let` filled.  Its copy,
; scope_lib_formed_names_renamed.metta, spells every variable and every
; binder of every clause differently and keeps the public names; no answer
; may change.
;   lib-*-closure  a closure that owns its local: each call has its own.
;                  A lambda formed from parts owns none of their names,
;                  which keep the scope they were written in (stage 5, item
;                  5), so the cons-atom closure's body writes `new`.
;   lib-*-cross    a closure over the caller's argument whose local is
;                  spelled as the caller spells its own name (cross-form
;                  capture).
;   lib-template   a template, code whose lambda owns its local, opened by
;                  template:map for each element.
!(import! &self template)
(= (lib-cons-closure) (cons-atom lam (z (new ($y) (let $y z (g $y))))))
(= (lib-star-closure) *@(lam z (let $y z (g $y))))
(= (lib-lift-let-closure) *(lift let k @z @(lam k (let $y k (g $y)))))
(= (lib-cons-cross) (cons-atom lam (x (cons-atom lam (z (let $w z (Pair $w x)))))))
(= (lib-star-cross) *@(lam x (lam z (let $w z (Pair $w x)))))
(= (lib-lift-let-cross)
   *(lift let (a b) (@x @z) @(lam a (lam b (let $w b (Pair $w a))))))
(= (lib-template $xs) (template:map $xs k @((lam z (let $u z (Pair $u $u))) k)))
