; Three thousand activations each leave a fresh variable in the answer, while
; collections run often enough to reuse frame identities: the variables stay
; distinct.
(= (opens $n) (if (== $n 0) () (cons (pair $n $x) (opens (- $n 1)))))
!(opens 3000)
