!(reset-runtime-stats!)
!(pragma! num-threads 2)

!(assertEqual
   (size-atom
     (collapse
       (hyperpose
         ((+ 1 2)
          (+ 2 3)))))
   2)
!(assertEqual
   (let $values
     (collapse
       (hyperpose
         ((+ 1 2)
          (+ 2 3))))
     (foldl-atom $values 0 $acc $item (eval (+ $acc $item))))
   8)

!(bind! &capture-stats (runtime-stats!))

; Receipts are diagnostic observations, not a required executor path.
; Whatever realization is selected, capture attempts must be accounted for
; exactly once as either a commit or a decline.
!(assertEqualToResult
   (match &capture-stats
     (runtime-counter hyperpose-source-family-capture-attempt $attempt)
     (match &capture-stats
       (runtime-counter hyperpose-source-family-capture-commit $commit)
       (match &capture-stats
         (runtime-counter hyperpose-source-family-capture-decline $decline)
         (== $attempt (+ $commit $decline)))))
   (True))
