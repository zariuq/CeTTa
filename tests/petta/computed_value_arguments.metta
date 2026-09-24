; A value that is already computed is data.  Passing it to a builtin does not
; evaluate the calls it contains again; only eval does that.  Expected values
; are read with sread too, so neither side is evaluated as source.
(= (nothing) (empty))
(= (loud) (println! evaluated-again))

; append and union-atom keep the value's inner call as data.
!(test (let $e (sread "((k (nothing)))") (append () $e))
       (sread "((k (nothing)))"))
!(test (let $e (sread "(k (nothing))") (append () ($e)))
       (sread "((k (nothing)))"))
!(test (let $e (sread "(k (nothing))") (union-atom () ($e)))
       (sread "((k (nothing)))"))
!(test (let $e (sread "((k (loud)))") (append $e ()))
       (sread "((k (loud)))"))

; Boolean relations compare the value without running it.
!(test (let $e (sread "((k (nothing)))") (not (== $e ()))) true)
!(test (let $e (sread "((k (nothing)))") (or False (== $e ()))) false)
!(test (let $e (sread "((k (nothing)))") (and True (== $e ()))) false)

; Membership and length read the value as a list.
!(test (let $e (sread "(k (nothing))") (is-member $e (a $e))) true)
!(test (let $e (sread "((k (nothing)) b)") (length $e)) 2)

; eval is the one place a computed value runs.
!(test (let $c (sread "(+ 1 2)") (eval $c)) 3)
