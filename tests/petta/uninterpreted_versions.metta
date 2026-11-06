; An uninterpreted argument with variables keeps its claim only through
; uninterpreted patterns: a let whose pattern names a declared constructor
; can bind the argument's variables to interpreted terms, so the version
; declines, and the calls answer as the protocol does.
(: Nil (List $t))
(: Cons (-> $t (List $t) (List $t)))
(: pin8 (-> $a Number $a))
(= (pin8 $x $n) (if (== $n 0) (let (Cons $k Nil) $x $x) (let ($g $m Nil) $x (let $s (pin8 $x (- $n 1)) $x))))
!(collapse (pin8 $y 1))
!(collapse (pin8 $y 0))
!(collapse (pin8 ($g $m Nil) 0))
!(collapse (get-type ($g $m Nil)))
!(collapse (get-type (Cons $k Nil)))
