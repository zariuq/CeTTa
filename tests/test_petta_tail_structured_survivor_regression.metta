(= (tail-build-list $n $accum)
   (if (== $n 0)
       $accum
       (tail-build-list (- $n 1) (cons $n $accum))))

!(reset-runtime-stats!)
!(assertEqualToResult
  (tail-build-list 8 ())
  ((1 2 3 4 5 6 7 8)))
!(bind! &tail-structured-stats (runtime-stats!))
!(assertEqualToResult
  (match &tail-structured-stats
    (runtime-counter eval-tail-reclaimed-bytes $n)
    (> $n 0))
  (True))
