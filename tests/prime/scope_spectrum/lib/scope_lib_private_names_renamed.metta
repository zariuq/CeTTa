; scope_lib_private_names.metta with every variable and every binder of
; every clause spelled differently; the public names are kept
; (tests/prime/routes/private_names_renamed.metta).
(= (lib-closure) (lam p (let $r p (g $r))))
(= (lib-template $list) (map-atom $list $item (let $v $item (Pair $v $v))))
(= (lib-crossing $s) (lam q (let (d $s $key) q (Got $key))){$s})
(= (lib-cross) (lam a (lam b (let $y b (Pair $y a)))))
