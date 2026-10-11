; A library elaborated under explicit-capture: its lambda owns every name its
; crossing set does not share, whoever imports it.
(scope:profile explicit-capture)
(= (lib-ec) (lam z (let $y z (g $y))))
(= (lib-ec-shared) (lam z (let (d $t) z ok)){$t})
