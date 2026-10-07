; A library elaborated under query-wide: its lambdas own nothing, whoever
; imports it.
(scope:profile query-wide)
(= (lib-qw) (lam z (let $y z (g $y))))
