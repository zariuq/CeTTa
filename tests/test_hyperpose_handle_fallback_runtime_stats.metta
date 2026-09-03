; A fresh space produced by a Hyperpose branch must remain an owned, usable
; value after the branch joins.  The scheduler used to preserve that lifetime
; is deliberately not part of this observable contract.

!(pragma! num-threads 2)
!(case (collapse (hyperpose ((new-space) ok)))
  ((($s ok) ())
   ($_ (Error hyperpose unexpected-handle-frontier))))
