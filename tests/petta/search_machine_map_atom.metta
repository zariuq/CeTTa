(= (identity-rel $x) $x)

; Two identical clauses make each item yield two occurrences.  Mapping two
; items must therefore retain all four product branches.
(= (duplicate-rel $x) $x)
(= (duplicate-rel $x) $x)

!(test (map-atom (a b) identity-rel) (a b))
!(test (map-atom () identity-rel) ())
!(test
    (collapse (map-atom (a b) duplicate-rel))
    ((a b) (a b) (a b) (a b)))

; The explicit-binder form substitutes independently for every item.
!(test (map-atom (1 2) $x (+ $x 1)) (2 3))

; Its list operand is demanded, rather than assumed to be inert tuple data.
!(test
    (map-atom (append (a) (b)) identity-rel)
    (a b))

; A non-list source has no relational answer.
!(test
    (collapse (map-atom not-a-list identity-rel))
    ())
