(= (native-predicate-inc $x) (+ $x 1))
(= (predicate-value $x) (Predicate (+ $x 1)))

; Predicate is the reified-call boundary: bindings flow into its body, but
; call-shaped syntax remains data until a predicate operation consumes it.
!(test
    (predicate-value 41)
    (quote (Predicate (+ 41 1))))

; The same syntax remains executable outside that boundary.
!(test (+ 41 1) 42)

!(test
    (progn
        (translatePredicate (is $x 2))
        (translatePredicate (+ $x 40 $z))
        $z)
    42)

!(test
    (progn
        (translatePredicate (native-predicate-inc 4 $out))
        $out)
    5)

!(test
    (collapse (translatePredicate (+ 2 40 41)))
    ())

!(test
    (translatePredicate (unknown-predicate a $x))
    (quote (translatePredicate (unknown-predicate a $x))))
