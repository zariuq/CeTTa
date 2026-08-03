; PeTTa's predicate namespaces create a named space on first insertion.
!(add-atom &implicit-space (item 1))
!(add-atom &implicit-space (item 1))
!(add-atom &implicit-space (item 2))

; The namespace remains a bag.
!(test
    (msort
      (collapse
        (match &implicit-space (item $x) (item $x))))
    ((item 1) (item 1) (item 2)))

; PeTTa remove-atom follows retractall/1 for an exact atom.
!(remove-atom &implicit-space (item 1))
!(test
    (collapse
      (match &implicit-space (item $x) (item $x)))
    ((item 2)))

; A negative query on an existing namespace has no answers.
!(test
    (collapse
      (match &implicit-space (missing $x) (missing $x)))
    ())

; The same first-add rule applies when an enclosing control form reaches the
; optimized direct-grounded path.
!(let $x 7 (add-atom &implicit-nested (item $x)))
!(test
    (collapse
      (match &implicit-nested (item $x) (item $x)))
    ((item 7)))
