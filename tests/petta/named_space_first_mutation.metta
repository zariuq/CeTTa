; Named PeTTa spaces begin empty. A first removal succeeds without a row.
!(remove-atom &first-remove (item $x))
!(test (collapse (match &first-remove $x $x)) ())
!(add-atom &first-remove (item 1))
!(add-atom &first-remove (item 1))
!(test (collapse (match &first-remove (item $x) $x)) (1 1))
!(remove-atom &first-remove (item $x))
!(test (collapse (match &first-remove $x $x)) ())

; The same rule applies when the space name crosses a function boundary.
(= (clear-items $space) (remove-atom $space (item $x)))
!(clear-items &parameter-space)
!(test (collapse (match &parameter-space $x $x)) ())
!(add-atom &parameter-space (item 2))
!(clear-items &parameter-space)
!(test (collapse (match &parameter-space $x $x)) ())
