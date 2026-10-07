; A library for the private-name invariance fixtures
; (tests/prime/routes/private_names.metta).  Its copy,
; scope_lib_private_names_renamed.metta, spells every variable and every
; binder of every clause differently and keeps the public names; no answer
; may change.
;   lib-closure   a closure that owns its local: each call has its own.
;   lib-template  a map-atom template that owns its local: each element
;                 has its own.
;   lib-crossing  a closure whose crossing set {$t} shares $t with the
;                 clause, here the caller's argument; it owns its other
;                 names.
;   lib-cross     a closure over the caller's argument whose local is
;                 spelled as the caller spells its own name (cross-form
;                 capture).
(= (lib-closure) (lam z (let $y z (g $y))))
(= (lib-template $xs) (map-atom $xs $e (let $u $e (Pair $u $u))))
(= (lib-crossing $t) (lam z (let (d $t $k) z (Got $k))){$t})
(= (lib-cross) (lam x (lam z (let $w z (Pair $w x)))))
