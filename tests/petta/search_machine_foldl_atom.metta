(= (append-step $acc $item) (append $acc ($item)))

; PeTTa's five-argument fold binds the accumulator and item variables
; lexically at every step.
!(test
    (foldl-atom (a b c) () $acc $item
        (append $acc ($item)))
    (a b c))

; The three-argument form applies a named callable to accumulator and item.
!(test
    (foldl-atom (a b c) () append-step)
    (a b c))

; The list and initial accumulator are computations, not inert syntax.
!(test
    (foldl-atom (append (a) (b)) (append () (seed))
        $acc $item (append $acc ($item)))
    (seed a b))

; Empty input returns the evaluated initial accumulator without running a
; step.
!(test
    (foldl-atom () (append (seed) (value))
        $acc $item (empty))
    (seed value))

; Step nondeterminism forms the ordinary depth-first product and preserves
; duplicate occurrences.
!(test
    (collapse
        (foldl-atom (a b) () $acc $item
            (superpose
                ((append $acc ($item))
                 (append $acc ($item))))))
    ((a b) (a b) (a b) (a b)))

; A failed step fails the fold branch; collapse observes an empty bag.
!(test
    (collapse
        (foldl-atom (a b) () $acc $item (empty)))
    ())

; A non-list source has no relational answer.
!(test
    (collapse
        (foldl-atom not-a-list seed append-step))
    ())
