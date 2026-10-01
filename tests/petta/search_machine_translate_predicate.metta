(= (native-predicate-inc $x) (+ $x 1))
(= (predicate-value $x) (Predicate (quote (+ $x 1))))
(= (predicate-sum $x) (Predicate (+ $x 1)))

; Predicate is a function: its argument is evaluated and the value names the
; term.  A quoted argument is the goal it spells, bindings included.
!(test
    (predicate-value 41)
    (Predicate (quote (+ 41 1))))

; Evaluated, (+ 41 1) is 42, which names no term.
!(test (collapse (predicate-sum 41)) ())

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

; A goal Prolog does not define raises its existence error, as the
; reference's compiled call does.
!(catch (translatePredicate (unknown-predicate a $x)))
