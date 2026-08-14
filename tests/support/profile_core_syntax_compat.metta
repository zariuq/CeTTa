; he-compat exposes Rust HE core semantics, not CeTTa core-syntax extensions.

!(foldl-atom (1 2 3) 0 $a $b (eval (+ $a $b)))

!(filter-atom (1 2 3) $x (eval (> $x 1)))

!(get-type range-atom)
!(range-atom 3)
!(repeat-atom 2 z)

!(add-atom-nodup bogus marker)
!(space-set-match-backend! bogus native-subst-tree)
