; map-atom and filter-atom come directly from the HE stdlib equations.
!(map-atom (1 2 3 4) $v (eval (+ $v 1)))
!(filter-atom (1 2 3 4) $v (eval (> $v 2)))
