; min and max are native: a program whose only stdlib names are theirs
; answers without starting the Prolog engine.
(= (clamp $v $lo $hi) (min $hi (max $v $lo)))

!(min 1.5 2.5)
!(max 3 4.5)
!(min 2 1)
!(clamp 5 0 3)
!(clamp -1.5 0 3)
!(clamp 1.25 0 3)

; Root and computed heads share the native owner. Quoted values remain
; data, and a relational argument keeps both occurrences.
!(let $f max ($f 3 4))
!(let $f min ($f 3 4))
!(quote (max 3 4))
!(let $v (quote (min 3 4)) $v)
!(max (superpose (1 5)) 3)
!(min (superpose (1 5)) 3)
