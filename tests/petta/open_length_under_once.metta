; The size of a list of open length is chosen where the size is asked for.
; A once around that size commits its first choice, the shortest length,
; which closes the tail; a once around the whole binding does the same.
!(let $x (append (1) $tail) (once (size-atom $x)))
!(once (let $x (append (1) $tail) (size-atom $x)))
!(length-and-tail
  (collapse (let $x (append (1) $t)
    (let $n (once (size-atom $x)) (got $n $t)))))
; The committed choice bound the tail: a later, longer tail fails.
!(length-then-incompatible-tail
  (collapse (let $x (append (1) $t)
    (let $n (once (size-atom $x))
      (let $t (2 3) (got $n $x))))))
!(collapse (let $x (append (1 2) $t) (once (size-atom $x))))
; A list of closed length has one size, however it was built.
!(let $x (1 2 3) (size-atom $x))
!(let $x (append (1 2) (3)) (size-atom $x))
