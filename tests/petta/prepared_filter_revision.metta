(= (revision-filter $x) (> $x 2))
!(filter-atom (1 2 3) $x (revision-filter $x))
!(let $changed (add-atom &self (= (revision-filter 1) True)) changed)
!(filter-atom (1 2 3) $x (revision-filter $x))
