; A finite fuel grant is one affine purse.  Scheduler configuration must not
; change the observed first result; an implementation may use any realization
; that accounts for the purse exactly once.

!(pragma! max-stack-depth 100)
!(pragma! num-threads 1)
!(assertEqual
  (once (hyperpose (bounded-left bounded-right)))
  bounded-left)
!(pragma! num-threads 2)
!(assertEqual
  (once (hyperpose (bounded-left bounded-right)))
  bounded-left)
