; The typechecker infers inc's signature in one block and reads it again
; when it checks a later one; the signature outlives the collections
; between them.
(= (inc $x) (+ $x 1))
!(inc 1)
!(collapse (inc 2))
(= (twice $x) (inc (inc $x)))
!(twice 1)
(= (shout $x) (inc "text"))
!(twice 2)
