; Borrowed nested scratch reset must not wipe parent sharing.
(= (mk-shared $x)
   (Top (Pair $x alpha) (Pair $x alpha)))

!(let* (
    ($warm (mk-shared foo))
    (() (reset-runtime-stats!))
    ($baseline (mk-shared foo))
    ($baseline-stats (runtime-stats!))
    ($baseline-inserts
      (match $baseline-stats (runtime-counter hashcons-insert $n1) $n1))
    (() (assertEqual (fold (superpose (1 2)) 0 $acc $item (+ $acc 1)) 2))
    (() (reset-runtime-stats!))
    ($after (mk-shared foo))
    ($after-stats (runtime-stats!))
    ($after-inserts
      (match $after-stats (runtime-counter hashcons-insert $n2) $n2))
  )
  (assertEqual (> $baseline-inserts $after-inserts) True))
