!(import_prolog_function
  (superpose
    (memoize config-memoize get-memoize-stats
     clear-memoize-stats)))

!(memoize choose)
(= (choose $x) $x)
(= (choose $x) (Pair $x $x))

!(choose 7)
!(choose 7)
!(get-memoize-stats)
!(clear-memoize-stats)

!(memoize shape-kind)
(= (shape-kind (Pair $x $y)) pair)
!(shape-kind (Pair $a 2))
!(shape-kind (Pair $b 2))
!(get-memoize-stats)

!(clear-memoize-stats)
!(config-memoize (aggregate sum))
!(memoize choices)
(= (choices $x) $x)
(= (choices $x) (+ $x 1))
(= (choices $x) (+ $x 2))
!(choices 5)
!(choices 5)
!(get-memoize-stats)
