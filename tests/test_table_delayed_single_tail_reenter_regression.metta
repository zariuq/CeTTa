; Regression: cached single-result variant-table replay should preserve the
; deterministic tail-reentry lane without eagerly baking semantic bindings into
; the result atom first.

(= (project1 $x)
   (Pair $x))

(= (single-via-eq $ss)
   (match $ss (: only $body)
     (project1 $body)))

!(bind! &ss (new-space))
!(add-atom &ss (: only (P $p $q)))

!(let* (
    (() (pragma! search-table-mode variant))
    (() (assertAlphaEqualToResult
          (single-via-eq &ss)
          ((Pair (P $a $b)))))
    (() (reset-runtime-stats!))
    (() (assertAlphaEqualToResult
          (single-via-eq &ss)
          ((Pair (P $a $b)))))
    ($stats (runtime-stats!))
    (() (assertEqualToResult
          (match $stats (runtime-counter table-hit $n) (> $n 0))
          (True)))
  )
  ())
