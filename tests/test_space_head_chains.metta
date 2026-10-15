; Declarations and equations are read through the chain of their head.

; Declared types: each head reads its own declarations, in declaration order.
(: area (-> Number Number))
(: label (-> Symbol String))
(: shift (-> Number Number))
(: shift (-> Symbol Symbol))
!(get-type area)
!(get-type label)
!(collapse (get-type shift))
; An undeclared head has no declaration of its neighbours.
!(get-type perimeter)

; An expression subject is read by its own head.
(: (Pair a b) PairType)
(: (Pair a) Single)
!(get-type (Pair a b))
!(get-type (Pair a))

; A removed declaration leaves the rest of its head.
!(remove-atom &self (: shift (-> Number Number)))
!(collapse (get-type shift))
!(get-type area)

; Equations: each head reads its own equations, of each arity.
(= (twice $x) (* 2 $x))
(= (twice $x $y) (+ $x $y))
(= (thrice $x) (* 3 $x))
!(twice 4)
!(twice 4 5)
!(thrice 4)
; No equation of that arity: the call stays as it is.
!(twice 1 2 3)
; No equation of that head.
!(halve 4)

; A removed equation leaves the other arity of its head.
!(remove-atom &self (= (twice $x $y) (+ $x $y)))
!(twice 4)
!(twice 4 5)

; A head added after the first lookups is found.
(= (later) found)
!(later)

; A subject or call whose head is an expression is read through the rows
; without a symbol head.
(: ((make 1) item) Made)
!(get-type ((make 1) item))
(= ((curry $x) $y) (pair $x $y))
!((curry 1) 2)
!(curry 1)
